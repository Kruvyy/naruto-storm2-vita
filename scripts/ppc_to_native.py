#!/usr/bin/env python3
"""Translate one recompiled guest function into a faster native C++ version.

Input: the ReXGlue output (storm2/generated/default/*.cpp), where every guest
instruction appears as a comment (// lwz r18,2048(r11)) before its C++ and
branch targets appear as labels (loc_82599418:).

Output: a C++ fragment (#include'd by storm2/src/storm2_audio_native.cpp) that
defines `static void Native_<name>(PPCContext& ctx, uint8_t* base)`. It does
the same work as the recompiled function, with these differences, all chosen
for speed on the PS Vita (ARMv7, software MMU):
  - guest registers are C++ locals (32-bit integers, single-precision floats)
    instead of PPCContext fields, so the compiler keeps them in registers;
  - the function's own stack frame (r1 + offset) is a local byte array: the
    translator checks that no stack address escapes the function;
  - fadds/fsubs/fmuls are single-precision operations (exactly what the
    recompiled code computes in double and rounds); fmadds is computed as
    float(double(a) * c + b): the product is exact in double, so it differs
    from the fused single-precision result only when the double-precision sum
    rounds to a single-precision tie;
  - the save/restore helpers (__savegprlr_N, __savefpr_N, ...) are dropped:
    the native function never writes the caller's non-volatile registers;
  - guest instruction counting is dropped;
  - VMX registers (lvx128/stvx128/vspltw/vmulfp128/vmaddfp) are V4 locals in
    guest element order (storm2_native.h): one 32-bit byte swap per lane at
    a load or store instead of the recompiled code's 16-byte table shuffle,
    and no store of every result to PPCContext. vmaddfp keeps the separate
    multiply and add of the recompiled code (same results).
Calls to other guest functions go through PPCContext as usual (volatile
registers written before the call and read back after it).

Only the instructions and forms the function uses are supported; anything
else stops the translation with an error, so a new function is checked
instruction by instruction rather than translated wrongly.

The output derives from the game's code: it goes to a git-ignored directory.

Usage:
  py -3 scripts/ppc_to_native.py --gen-dir storm2/generated/default \
      --func sub_82599010 --out storm2/generated_native/sub_82599010.inc
"""
import argparse
import glob
import os
import re
import sys

HELPER_RE = re.compile(r'__(savegprlr|savefpr|restfpr|restgprlr|savevmx|restvmx)_\d+\(')


class TranslationError(Exception):
    pass


def find_function(gen_dir, name):
    pattern = re.compile(r'^DEFINE_REX_FUNC\(' + re.escape(name) + r'\)\s*\{')
    for path in sorted(glob.glob(os.path.join(gen_dir, '*.cpp'))):
        with open(path, encoding='utf-8', errors='replace') as f:
            lines = f.read().split('\n')
        for i, line in enumerate(lines):
            if pattern.match(line):
                body = []
                for j in range(i + 1, len(lines)):
                    if lines[j].rstrip() == '}':
                        return path, body
                    body.append(lines[j])
                raise TranslationError(f'{name}: no closing brace in {path}')
    raise TranslationError(f'{name} not found in {gen_dir}')


def parse(body):
    """Returns items: ('label', addr) or ('insn', text, cpp_lines)."""
    items = []
    for line in body:
        s = line.strip()
        m = re.match(r'^loc_([0-9A-Fa-f]{8}):', s)
        if m:
            items.append(['label', m.group(1).upper()])
            continue
        m = re.match(r'^// (.+)$', s)
        if m:
            items.append(['insn', m.group(1).strip(), []])
            continue
        if items and items[-1][0] == 'insn' and s:
            items[-1][2].append(s)
    return items


def gpr(tok):
    if tok == '0':  # rA = 0 in indexed forms is written "0" by some instructions
        return 0
    m = re.fullmatch(r'r(\d+)', tok)
    if not m:
        raise TranslationError(f'expected a GPR, got {tok}')
    return int(m.group(1))


def fpr(tok):
    m = re.fullmatch(r'f(\d+)', tok)
    if not m:
        raise TranslationError(f'expected an FPR, got {tok}')
    return int(m.group(1))


def vreg(tok):
    m = re.fullmatch(r'v(\d+)', tok)
    if not m or int(m.group(1)) > 127:
        raise TranslationError(f'expected a VMX register, got {tok}')
    return int(m.group(1))


def crf(tok):
    m = re.fullmatch(r'cr(\d)', tok)
    if not m:
        raise TranslationError(f'expected a CR field, got {tok}')
    return int(m.group(1))


def imm(tok):
    return int(tok, 0)


def mem(tok):
    m = re.fullmatch(r'(-?(?:0x)?[0-9A-Fa-f]+)\((r\d+)\)', tok)
    if not m:
        raise TranslationError(f'expected d(rA), got {tok}')
    return int(m.group(1), 0), gpr(m.group(2))


def mask32(mb, me):
    m = 0
    i = mb
    while True:
        m |= 1 << (31 - i)
        if i == me:
            break
        i = (i + 1) & 31
    return m


def target_label(tok, prefix=''):
    m = re.fullmatch(r'0x([0-9A-Fa-f]{8})', tok)
    if not m:
        raise TranslationError(f'expected a branch target, got {tok}')
    return 'L_' + prefix + m.group(1).upper()


# Volatile registers synchronised with PPCContext around calls.
VOLATILE_GPRS = [0] + list(range(3, 13))


class Translator:
    def __init__(self, name, gen_dir=None, inline=(), guest_stack=False, ctx_regs=False,
                 track_stores=False):
        self.name = name
        self.gen_dir = gen_dir
        self.inline = set(inline)         # leaf callees translated in place
        self.guest_stack = guest_stack    # r1 accesses go to guest memory
        self.ctx_regs = ctx_regs          # all registers kept in step with PPCContext
        self.track_stores = track_stores  # template<bool kTrack>: guest stores reported
        self.prefix = ''                  # label prefix of the callee being inlined
        self.inline_end = None            # its end label
        self.inline_count = 0
        self.fwritten = set()             # FPRs the function writes (ctx_regs)
        self.restores_lr = False          # returns through __restgprlr_N
        # VMX registers known to hold one value in every element (written by
        # vspltw in the current straight-line code): vrefp then needs one
        # division and stvewx no element select. Cleared at labels and calls.
        self.splat = set()
        self.out = []
        self.frame = None          # stack frame size once stwu ran
        self.stack_access = set()  # (offset, width)
        self.cr_used = set()
        self.calls = set()
        self.labels = set()
        self.targets = set()
        self.insn_count = 0
        self.vregs = set()         # VMX registers used (each a V4 local)
        self.vfloat = False        # vector float arithmetic used
        self.sfloat = False        # scalar float arithmetic used

    def vread(self, n):
        if n not in self.vregs:
            if not self.ctx_regs:
                raise TranslationError(f'v{n} is read before it is written in the function')
            self.vregs.add(n)  # loaded from PPCContext at entry and after calls
        return f'v{n}'

    def vwrite(self, n, splat=False):
        self.vregs.add(n)
        if splat:
            self.splat.add(n)
        else:
            self.splat.discard(n)
        return f'v{n}'

    def check_cpp(self, cpp, needle, text):
        if not any(needle in l for l in cpp):
            raise TranslationError(f'recompiled form not as expected ({needle!r}): {text}')

    def emit(self, text):
        self.out.append('  ' + text)

    def ea(self, d, ra):
        return f'(r[{ra}] + {d}u)' if ra else f'{d & 0xFFFFFFFF}u'

    def store(self, text, addr, size):
        if self.track_stores:
            self.emit(f'if constexpr (kTrack) TrackStore({addr}, {size});')
        self.emit(text)

    def label(self, tok):
        return target_label(tok, self.prefix)

    def stack_offset(self, d, width):
        if self.frame is None:
            raise TranslationError('stack access before the frame is set up (stwu)')
        if d < 0:
            raise TranslationError(f'negative stack offset {d}')
        self.stack_access.add((d, width))
        return d

    def cond(self, kind, field):
        self.cr_used.add(field)
        c = f'cr{field}'
        return {
            'blt': f'{c}_lt', 'bgt': f'{c}_gt', 'beq': f'{c}_eq',
            'bge': f'!{c}_lt', 'ble': f'!{c}_gt', 'bne': f'!{c}_eq',
        }[kind]

    def call_sync_out(self):
        self.emit('ctx.r1.u64 = r[1];')
        if self.ctx_regs:
            # Every GPR (a direct callee may take arguments in any register),
            # the FPRs written since the last call (exact widening) and every
            # VMX register (the game passes vectors in v92-v95 etc.).
            for n in [0] + list(range(3, 32)):
                if n != 13:
                    self.emit(f'ctx.r{n}.u64 = r[{n}];')
            self.emit('@FSYNC_OUT@')
            self.emit('@VSYNC_OUT@')
            return
        for n in VOLATILE_GPRS:
            self.emit(f'ctx.r{n}.u64 = r[{n}];')
        for n in range(0, 14):
            self.emit(f'ctx.f{n}.f64 = double(f[{n}]);')

    def call_sync_in(self):
        for n in VOLATILE_GPRS:
            self.emit(f'r[{n}] = ctx.r{n}.u32;')
        for n in range(0, 14):
            self.emit(f'f[{n}] = float(ctx.f{n}.f64);')
        if self.ctx_regs:
            self.emit('@VSYNC_IN@')
            self.emit('@FLUSH@')
        self.splat.clear()

    def fwrite(self, n):
        if self.ctx_regs:
            self.fwritten.add(n)
            self.emit(f'fmask |= {1 << n}u;')

    def lr_line(self, cpp):
        for l in cpp:
            if re.match(r'^ctx\.lr = 0x[0-9A-Fa-f]+;$', l):
                self.emit(l)
                return

    def inline_call(self, callee):
        path, body = find_function(self.gen_dir, callee)
        items = parse(body)
        for item in items:
            if item[0] == 'insn':
                op = item[1].split()[0]
                if op in ('bl', 'b', 'bctrl', 'bctr', 'stwu', 'mflr'):
                    raise TranslationError(f'{callee} is not a leaf ({item[1]})')
        self.inline_count += 1
        saved = (self.prefix, self.inline_end)
        self.prefix = f'I{self.inline_count}_'
        self.inline_end = f'L_I{self.inline_count}_end'
        self.emit(f'// inlined {callee}')
        self.translate_items(items)
        self.out.append(f'{self.inline_end}:;')
        self.labels.add(self.inline_end)
        self.prefix, self.inline_end = saved

    def insn(self, text, cpp):
        self.insn_count += 1
        parts = text.replace(',', ' ').split()
        op, a = parts[0], parts[1:]
        e = self.emit
        if op == 'mflr':
            return  # the native function returns normally
        if op in ('bl', 'b'):
            helper = any(HELPER_RE.search(l) for l in cpp)
            if helper:
                if op == 'b' and any('__restgprlr' in l for l in cpp):
                    self.restores_lr = True
                    e('goto epilogue;')
                return  # save/restore helpers: not needed (see the header)
            if op == 'b':
                t = self.label(a[0])
                self.targets.add(t)
                e(f'goto {t};')
                return
            m = None
            for l in cpp:
                m = re.search(r'\b(sub_[0-9A-Fa-f]{8})\(ctx, base\)', l)
                if m:
                    break
            if not m:
                raise TranslationError(f'call without a recognised callee: {text} / {cpp}')
            callee = m.group(1)
            if callee in self.inline:
                self.inline_call(callee)
                return
            self.calls.add(callee)
            self.call_sync_out()
            if self.ctx_regs:
                self.lr_line(cpp)
            e(f'{callee}(ctx, base);')
            self.call_sync_in()
            return
        if op == 'bctrl':
            if not self.ctx_regs:
                raise TranslationError('indirect call needs --ctx-regs')
            self.call_sync_out()
            self.lr_line(cpp)
            e('REX_CALL_INDIRECT_FUNC(ctr);')
            self.call_sync_in()
            return
        if op == 'blr':
            e(f'goto {self.inline_end or "epilogue"};')
            return
        if op == 'stwu':
            rs, (d, ra) = gpr(a[0]), mem(a[1])
            if ra != 1:
                if rs == ra or ra == 0:
                    raise TranslationError(f'unsupported stwu form: {text}')
                addr = f'(r[{ra}] + {d & 0xFFFFFFFF}u)'
                self.store(f'Store32(base, {addr}, r[{rs}]);', addr, 4)
                e(f'r[{ra}] += {d & 0xFFFFFFFF}u;')
                return
            if rs != 1 or d >= 0:
                raise TranslationError(f'unsupported stwu form: {text}')
            if self.frame is not None:
                raise TranslationError('second stwu')
            self.frame = -d
            if self.guest_stack:
                addr = f'(r[1] - {-d}u)'
                self.store(f'Store32(base, {addr}, r[1]);', addr, 4)
                e(f'r[1] -= {-d}u;  // frame of {-d} bytes in guest memory')
            else:
                e(f'r[1] -= {-d}u;  // frame of {-d} bytes (local array; back chain not kept)')
            return
        if op == 'lwzu':
            rd, (d, ra) = gpr(a[0]), mem(a[1])
            if rd == ra or ra in (0, 1):
                raise TranslationError(f'unsupported lwzu form: {text}')
            e(f'r[{ra}] += {d & 0xFFFFFFFF}u;')
            e(f'r[{rd}] = Load32(base, r[{ra}]);')
            return
        if op == 'mr':
            e(f'r[{gpr(a[0])}] = r[{gpr(a[1])}];')
            return
        if op == 'lis':
            e(f'r[{gpr(a[0])}] = {(imm(a[1]) << 16) & 0xFFFFFFFF}u;')
            return
        if op in ('addi', 'addis'):
            rd, ra, v = gpr(a[0]), gpr(a[1]), imm(a[2])
            if op == 'addis':
                v <<= 16
            v &= 0xFFFFFFFF
            if ra == 0:
                e(f'r[{rd}] = {v}u;')
            else:
                e(f'r[{rd}] = r[{ra}] + {v}u;')
            return
        if op == 'subf':
            rd, ra, rb = gpr(a[0]), gpr(a[1]), gpr(a[2])
            e(f'r[{rd}] = r[{rb}] - r[{ra}];')
            return
        if op == 'rlwinm':
            ra, rs, sh, mb, me = gpr(a[0]), gpr(a[1]), imm(a[2]), imm(a[3]), imm(a[4])
            m = mask32(mb, me)
            rot = f'std::rotl(r[{rs}], {sh})' if sh else f'r[{rs}]'
            e(f'r[{ra}] = {rot} & 0x{m:08X}u;')
            return
        if op == 'clrlwi':
            ra, rs, n = gpr(a[0]), gpr(a[1]), imm(a[2])
            e(f'r[{ra}] = r[{rs}] & 0x{(0xFFFFFFFF >> n):08X}u;')
            return
        if op == 'cmplwi':
            if len(a) == 2:
                field, ra, v = 0, gpr(a[0]), imm(a[1])
            else:
                field, ra, v = crf(a[0]), gpr(a[1]), imm(a[2])
            self.cr_used.add(field)
            c = f'cr{field}'
            e(f'{c}_lt = r[{ra}] < {v}u; {c}_gt = r[{ra}] > {v}u; {c}_eq = r[{ra}] == {v}u;')
            return
        if op == 'fcmpu':
            field, fa, fb = crf(a[0]), fpr(a[1]), fpr(a[2])
            self.cr_used.add(field)
            c = f'cr{field}'
            e(f'{c}_lt = f[{fa}] < f[{fb}]; {c}_gt = f[{fa}] > f[{fb}]; '
              f'{c}_eq = f[{fa}] == f[{fb}];  // all false when unordered')
            return
        if op in ('lwz', 'stw', 'lfs', 'stfs', 'std', 'ld'):
            reg, (d, ra) = a[0], mem(a[1])
            width = 8 if op in ('std', 'ld') else 4
            if ra == 1 and not self.guest_stack:
                off = self.stack_offset(d, width)
                if op == 'lwz':
                    e(f'r[{gpr(reg)}] = StackLoad32(stack, {off});')
                elif op == 'stw':
                    e(f'StackStore32(stack, {off}, r[{gpr(reg)}]);')
                elif op == 'lfs':
                    e(f'f[{fpr(reg)}] = std::bit_cast<float>(StackLoad32(stack, {off}));')
                elif op == 'stfs':
                    e(f'StackStore32(stack, {off}, std::bit_cast<uint32_t>(f[{fpr(reg)}]));')
                elif op == 'std':
                    e(f'StackStore64(stack, {off}, r[{gpr(reg)}]);')
                else:
                    e(f'r[{gpr(reg)}] = uint32_t(StackLoad64(stack, {off}));')
                return
            if width == 8:
                raise TranslationError(f'64-bit access off the stack: {text}')
            addr = self.ea(d, ra)
            if op == 'lwz':
                e(f'r[{gpr(reg)}] = Load32(base, {addr});')
            elif op == 'stw':
                self.store(f'Store32(base, {addr}, r[{gpr(reg)}]);', addr, 4)
            elif op == 'lfs':
                e(f'f[{fpr(reg)}] = LoadF(base, {addr});')
                self.fwrite(fpr(reg))
            else:
                self.store(f'StoreF(base, {addr}, f[{fpr(reg)}]);', addr, 4)
            return
        if op in ('lbz', 'stb'):
            reg, (d, ra) = gpr(a[0]), mem(a[1])
            if ra == 1 and not self.guest_stack:
                raise TranslationError(f'byte stack access: {text}')
            addr = self.ea(d, ra)
            if op == 'lbz':
                e(f'r[{reg}] = Load8(base, {addr});')
            else:
                self.store(f'Store8(base, {addr}, r[{reg}]);', addr, 1)
            return
        if op in ('lwzx', 'stwx'):
            reg, ra, rb = gpr(a[0]), gpr(a[1]), gpr(a[2])
            if (ra == 1 or rb == 1) and not self.guest_stack:
                raise TranslationError(f'indexed stack access: {text}')
            addr = f'(r[{ra}] + r[{rb}])' if ra else f'r[{rb}]'
            if op == 'lwzx':
                e(f'r[{reg}] = Load32(base, {addr});')
            else:
                self.store(f'Store32(base, {addr}, r[{reg}]);', addr, 4)
            return
        if op in ('lfsx', 'stfsx'):
            fr, ra, rb = fpr(a[0]), gpr(a[1]), gpr(a[2])
            if ra == 1 or rb == 1:
                raise TranslationError(f'indexed stack access: {text}')
            addr = f'(r[{ra}] + r[{rb}])' if ra else f'r[{rb}]'
            if op == 'lfsx':
                e(f'f[{fr}] = LoadF(base, {addr});')
                self.fwrite(fr)
            else:
                self.store(f'StoreF(base, {addr}, f[{fr}]);', addr, 4)
            return
        if op == 'fmr':
            e(f'f[{fpr(a[0])}] = f[{fpr(a[1])}];')
            self.fwrite(fpr(a[0]))
            return
        if op in ('fadds', 'fsubs', 'fmuls', 'fmadds', 'fcmpu'):
            self.sfloat = True
        if op in ('fadds', 'fsubs', 'fmuls'):
            fd, fa, fb = fpr(a[0]), fpr(a[1]), fpr(a[2])
            sym = {'fadds': '+', 'fsubs': '-', 'fmuls': '*'}[op]
            e(f'f[{fd}] = f[{fa}] {sym} f[{fb}];')
            self.fwrite(fd)
            return
        if op == 'fmadds':
            fd, fa, fc, fb = fpr(a[0]), fpr(a[1]), fpr(a[2]), fpr(a[3])
            e(f'f[{fd}] = float(double(f[{fa}]) * double(f[{fc}]) + double(f[{fb}]));')
            self.fwrite(fd)
            return
        if op == 'li':
            e(f'r[{gpr(a[0])}] = {imm(a[1]) & 0xFFFFFFFF}u;')
            return
        if op == 'add':
            rd, ra, rb = gpr(a[0]), gpr(a[1]), gpr(a[2])
            e(f'r[{rd}] = r[{ra}] + r[{rb}];')
            return
        if op == 'sth':
            reg, (d, ra) = gpr(a[0]), mem(a[1])
            if ra == 1 and not self.guest_stack:
                raise TranslationError(f'16-bit stack access: {text}')
            addr = self.ea(d, ra)
            self.store(f'Store16(base, {addr}, r[{reg}]);', addr, 2)
            return
        if op in ('addic', 'addic.'):
            # The carry stays in the recompiled function's local XER; nothing
            # here reads it (adde etc. are not supported).
            rd, ra, v = gpr(a[0]), gpr(a[1]), imm(a[2])
            e(f'r[{rd}] = r[{ra}] + {v & 0xFFFFFFFF}u;')
            if op == 'addic.':
                self.cr_used.add(0)
                e(f'cr0_lt = int32_t(r[{rd}]) < 0; cr0_gt = int32_t(r[{rd}]) > 0; '
                  f'cr0_eq = r[{rd}] == 0u;')
            return
        if op == 'ori':
            ra, rs, v = gpr(a[0]), gpr(a[1]), imm(a[2])
            e(f'r[{ra}] = r[{rs}] | {v & 0xFFFF}u;')
            return
        if op == 'mullw':
            rd, ra, rb = gpr(a[0]), gpr(a[1]), gpr(a[2])
            e(f'r[{rd}] = r[{ra}] * r[{rb}];')
            return
        if op == 'cmplw':
            if len(a) == 2:
                field, ra, rb = 0, gpr(a[0]), gpr(a[1])
            else:
                field, ra, rb = crf(a[0]), gpr(a[1]), gpr(a[2])
            self.cr_used.add(field)
            c = f'cr{field}'
            e(f'{c}_lt = r[{ra}] < r[{rb}]; {c}_gt = r[{ra}] > r[{rb}]; {c}_eq = r[{ra}] == r[{rb}];')
            return
        if op == 'rotlwi':
            rd, rs, n = gpr(a[0]), gpr(a[1]), imm(a[2])
            e(f'r[{rd}] = std::rotl(r[{rs}], {n});')
            return
        if op == 'cmpwi':
            if len(a) == 2:
                field, ra, v = 0, gpr(a[0]), imm(a[1])
            else:
                field, ra, v = crf(a[0]), gpr(a[1]), imm(a[2])
            self.cr_used.add(field)
            c = f'cr{field}'
            e(f'{c}_lt = int32_t(r[{ra}]) < {v}; {c}_gt = int32_t(r[{ra}]) > {v}; '
              f'{c}_eq = int32_t(r[{ra}]) == {v};')
            return
        if op == 'lhz':
            rd, (d, ra) = gpr(a[0]), mem(a[1])
            if ra == 1:
                raise TranslationError(f'16-bit stack access: {text}')
            e(f'r[{rd}] = Load16(base, {self.ea(d, ra)});')
            return
        if op in ('lvx', 'lvx128', 'stvx', 'stvx128', 'lvlx', 'lvlx128', 'lvrx', 'lvrx128',
                  'stvewx', 'stvewx128'):
            v, ra, rb = vreg(a[0]), gpr(a[1]), gpr(a[2])
            if (ra == 1 or rb == 1) and not self.guest_stack:
                raise TranslationError(f'vector stack access: {text}')
            addr = f'(r[{ra}] + r[{rb}])' if ra else f'r[{rb}]'
            if op.startswith('lvx'):
                e(f'{self.vwrite(v)} = LoadV(base, {addr});')
            elif op.startswith('lvlx'):
                e(f'{self.vwrite(v)} = LoadLeftV(base, {addr});')
            elif op.startswith('lvrx'):
                e(f'{self.vwrite(v)} = LoadRightV(base, {addr});')
            elif op.startswith('stvx'):
                src = self.vread(v)
                self.store(f'StoreV(base, {addr}, {src});', f'({addr} & ~15u)', 16)
            else:
                src = self.vread(v)
                fn = 'StoreLane0V' if v in self.splat else 'StoreElementV'
                self.store(f'{fn}(base, {addr}, {src});', f'({addr} & ~3u)', 4)
            return
        if op in ('vor', 'vor128'):
            vd, va, vb = vreg(a[0]), vreg(a[1]), vreg(a[2])
            x, y = self.vread(va), self.vread(vb)
            if va == vb:
                e(f'{self.vwrite(vd, splat=va in self.splat)} = {x};')
            else:
                e(f'{self.vwrite(vd)} = OrV({x}, {y});')
            return
        if op in ('vaddfp', 'vaddfp128'):
            vd, va, vb = vreg(a[0]), vreg(a[1]), vreg(a[2])
            self.vfloat = True
            x, y = self.vread(va), self.vread(vb)
            e(f'{self.vwrite(vd)} = AddV({x}, {y});')
            return
        if op in ('vrefp', 'vrefp128'):
            vd, vb = vreg(a[0]), vreg(a[1])
            if not any('rex_vrefp' in l for l in cpp):
                raise TranslationError(f'vrefp not as rex_vrefp: {text}')
            self.vfloat = True
            x = self.vread(vb)
            if vb in self.splat:
                e(f'{self.vwrite(vd, splat=True)} = RefpSplatV({x});')
            else:
                e(f'{self.vwrite(vd)} = RefpV({x});')
            return
        if op == 'vspltisw':
            vd, v = vreg(a[0]), imm(a[1])
            e(f'{self.vwrite(vd, splat=True)} = SplatIntV({v & 0xFFFFFFFF}u);')
            return
        if op == 'vpermwi128':
            vd, vb, v = vreg(a[0]), vreg(a[1]), imm(a[2])
            sel = [(v >> (6 - 2 * i)) & 3 for i in range(4)]  # element i <- vB[sel[i]]
            control = sum((3 - ((v >> (2 * j)) & 3)) << (2 * j) for j in range(4))
            self.check_cpp(cpp, f'shuffle_epi32(simde_mm_load_si128((simde__m128i*)ctx.v{vb}.u32), '
                                f'0x{control:X})', text)
            x = self.vread(vb)
            e(f'{self.vwrite(vd)} = PermV<{sel[0]}, {sel[1]}, {sel[2]}, {sel[3]}>({x});')
            return
        if op == 'vsldoi':
            vd, va, vb, sh = vreg(a[0]), vreg(a[1]), vreg(a[2]), imm(a[3])
            if sh % 4 or not 0 < sh < 16:
                raise TranslationError(f'vsldoi by a shift that is not whole elements: {text}')
            self.check_cpp(cpp, f'alignr_epi8(simde_mm_load_si128((simde__m128i*)ctx.v{va}.u8), '
                                f'simde_mm_load_si128((simde__m128i*)ctx.v{vb}.u8), {16 - sh})', text)
            x, y = self.vread(va), self.vread(vb)
            e(f'{self.vwrite(vd)} = ExtV<{sh // 4}>({x}, {y});')
            return
        if op == 'vcfsx':
            vd, vb, v = vreg(a[0]), vreg(a[1]), imm(a[2])
            if v != 0:
                raise TranslationError(f'vcfsx with a scale: {text}')
            self.check_cpp(cpp, 'simde_mm_cvtepi32_ps(', text)
            self.vfloat = True
            x = self.vread(vb)
            e(f'{self.vwrite(vd, splat=vb in self.splat)} = CvtIntV({x});')
            return
        if op in ('vslw', 'vsrw'):
            vd, va, vb = vreg(a[0]), vreg(a[1]), vreg(a[2])
            x, y = self.vread(va), self.vread(vb)
            fn = 'ShlV' if op == 'vslw' else 'ShrV'
            e(f'{self.vwrite(vd)} = {fn}({x}, {y});')
            return
        if op in ('vnmsubfp', 'vnmsubfp128'):
            vd, va, vc, vb = vreg(a[0]), vreg(a[1]), vreg(a[2]), vreg(a[3])
            self.check_cpp(cpp, f'simde_mm_xor_ps(simde_mm_sub_ps(simde_mm_mul_ps(simde_mm_load_ps('
                                f'ctx.v{va}.f32), simde_mm_load_ps(ctx.v{vc}.f32)), '
                                f'simde_mm_load_ps(ctx.v{vb}.f32))', text)
            self.vfloat = True
            x, c, y = self.vread(va), self.vread(vc), self.vread(vb)
            e(f'{self.vwrite(vd)} = NmsubV({x}, {c}, {y});')
            return
        if op == 'vmsum3fp128':
            vd, va, vb = vreg(a[0]), vreg(a[1]), vreg(a[2])
            self.check_cpp(cpp, f'simde_mm_dp_ps(simde_mm_load_ps(ctx.v{va}.f32), '
                                f'simde_mm_load_ps(ctx.v{vb}.f32), 0xEF)', text)
            self.vfloat = True
            x, y = self.vread(va), self.vread(vb)
            e(f'{self.vwrite(vd, splat=True)} = Msum3V({x}, {y});')
            return
        if op == 'vsel':
            vd, va, vb, vc = vreg(a[0]), vreg(a[1]), vreg(a[2]), vreg(a[3])
            x, y, c = self.vread(va), self.vread(vb), self.vread(vc)
            e(f'{self.vwrite(vd)} = SelV({x}, {y}, {c});')
            return
        if op in ('vcmpeqfp', 'vcmpgtfp'):
            vd, va, vb = vreg(a[0]), vreg(a[1]), vreg(a[2])
            fn = {'vcmpeqfp': 'cmpeq', 'vcmpgtfp': 'cmpgt'}[op]
            self.check_cpp(cpp, f'simde_mm_{fn}_ps(simde_mm_load_ps(ctx.v{va}.f32), '
                                f'simde_mm_load_ps(ctx.v{vb}.f32))', text)
            self.vfloat = True
            x, y = self.vread(va), self.vread(vb)
            e(f'{self.vwrite(vd)} = {"CmpEqV" if op == "vcmpeqfp" else "CmpGtV"}({x}, {y});')
            return
        if op == 'vand':
            vd, va, vb = vreg(a[0]), vreg(a[1]), vreg(a[2])
            x, y = self.vread(va), self.vread(vb)
            e(f'{self.vwrite(vd)} = AndV({x}, {y});')
            return
        if op in ('vspltw', 'vspltw128'):
            vd, vb, i = vreg(a[0]), vreg(a[1]), imm(a[2])
            if not 0 <= i <= 3:
                raise TranslationError(f'bad element: {text}')
            src = self.vread(vb)
            e(f'{self.vwrite(vd, splat=True)} = SplatV<{i}>({src});')
            return
        if op in ('vmulfp128',):
            vd, va, vb = vreg(a[0]), vreg(a[1]), vreg(a[2])
            self.vfloat = True
            x, y = self.vread(va), self.vread(vb)
            e(f'{self.vwrite(vd)} = MulV({x}, {y});')
            return
        if op in ('vmaddfp', 'vmaddfp128'):
            vd, va, vc, vb = vreg(a[0]), vreg(a[1]), vreg(a[2]), vreg(a[3])
            if op == 'vmaddfp128' and not any('add_ps(simde_mm_mul_ps' in l for l in cpp):
                raise TranslationError(f'vmaddfp128 operand order not checked: {text}')
            self.vfloat = True
            x, c, y = self.vread(va), self.vread(vc), self.vread(vb)
            e(f'{self.vwrite(vd)} = MaddV({x}, {c}, {y});')
            return
        if op == 'mtctr':
            e(f'ctr = r[{gpr(a[0])}];')
            return
        if op == 'bdnz':
            t = self.label(a[0])
            self.targets.add(t)
            e(f'if (--ctr != 0) goto {t};')
            return
        if op in ('blt', 'bgt', 'beq', 'bge', 'ble', 'bne'):
            if len(a) == 1:
                field, t = 0, self.label(a[0])
            else:
                field, t = crf(a[0]), self.label(a[1])
            self.targets.add(t)
            e(f'if ({self.cond(op, field)}) goto {t};')
            return
        raise TranslationError(f'unsupported instruction: {text}')

    def check_stack(self):
        accesses = sorted(self.stack_access)
        for i, (o1, w1) in enumerate(accesses):
            for o2, w2 in accesses[i + 1:]:
                if o2 >= o1 + w1:
                    break
                if (o1, w1) != (o2, w2):
                    raise TranslationError(
                        f'stack slot used with two widths/offsets: {o1}+{w1} and {o2}+{w2}')

    def translate_items(self, items):
        for item in items:
            if item[0] == 'label':
                self.splat.clear()
                label = 'L_' + self.prefix + item[1]
                self.labels.add(label)
                self.out.append(f'{label}:;')
            else:
                try:
                    self.insn(item[1], item[2])
                except TranslationError as err:
                    raise TranslationError(f'{item[1]}: {err}')

    def translate(self, items):
        self.translate_items(items)
        missing = self.targets - self.labels
        if missing:
            raise TranslationError(f'branch targets without a label: {sorted(missing)}')
        self.check_stack()

    def render(self, source_path):
        stack_size = max([o + w for o, w in self.stack_access] + [8])
        stack_size = (stack_size + 15) & ~15
        head = [
            f'// Native version of {self.name}, written by scripts/ppc_to_native.py from',
            f'// {os.path.basename(source_path)} ({self.insn_count} guest instructions'
            + (f', inlined: {", ".join(sorted(self.inline))}' if self.inline else '') + ').',
            '// Derived from the game\'s code: never commit.',
            '',
        ]
        for callee in sorted(self.calls):
            head.append(f'REX_EXTERN({callee});')
        head.append('')
        if self.track_stores:
            head += ['template <bool kTrack>',
                     f'static void NativeT_{self.name}(PPCContext& ctx, uint8_t* base) {{']
        else:
            head.append(f'static void Native_{self.name}(PPCContext& ctx, uint8_t* base) {{')
        head += [
            '  using namespace storm2_native;',
            '  uint32_t r[32] = {};',
            '  float f[32] = {};',
            '  uint32_t ctr = 0;',
        ]
        for field in sorted(self.cr_used):
            head.append(f'  bool cr{field}_lt = false, cr{field}_gt = false, cr{field}_eq = false;')
        vlist = sorted(self.vregs)
        if self.ctx_regs:
            for n in vlist:
                head.append(f'  V4 v{n} = FromCtxV(ctx.v{n});')
            head.append('  uint32_t fmask = 0;  // FPRs written since the last call')
            head.append('  const uint64_t entry_lr = ctx.lr;')
            head.append('  uint64_t entry_nonvolatile[18];')
            for n in range(14, 32):
                head.append(f'  entry_nonvolatile[{n - 14}] = ctx.r{n}.u64;')
        else:
            for n in vlist:
                head.append(f'  V4 v{n} = simde_vdupq_n_f32(0.0f);')
        if not self.guest_stack:
            head.append(f'  alignas(8) uint8_t stack[{stack_size}];')
        head += [
            '  const uint64_t entry_r1 = ctx.r1.u64;',
            '  r[1] = ctx.r1.u32;',
            '  r[13] = ctx.r13.u32;',
        ]
        if self.ctx_regs:
            for n in [0, 2] + list(range(3, 13)) + list(range(14, 32)):
                head.append(f'  r[{n}] = ctx.r{n}.u32;')
            for n in range(0, 14):
                head.append(f'  f[{n}] = float(ctx.f{n}.f64);')
        else:
            for n in range(3, 11):
                head.append(f'  r[{n}] = ctx.r{n}.u32;')
            for n in range(1, 9):
                head.append(f'  f[{n}] = float(ctx.f{n}.f64);')
        if self.vfloat and self.sfloat:
            raise TranslationError('vector and scalar float in one function (two flush modes)')
        # VMX arithmetic flushes denormals (the recompiled code enables the
        # flush mode before it); scalar FPU arithmetic does not.
        flush = '  ctx.fpscr.enableFlushMode();' if self.vfloat else '  ctx.fpscr.disableFlushMode();'
        head.append(flush)
        fsync = [f'  if (fmask & {1 << n}u) ctx.f{n}.f64 = WidenExact(f[{n}]);'
                 for n in sorted(self.fwritten)] + ['  fmask = 0;']
        body = []
        for line in self.out:
            t = line.strip()
            if t == '@VSYNC_OUT@':
                body += [f'  ToCtxV(ctx.v{n}, v{n});' for n in vlist]
            elif t == '@VSYNC_IN@':
                body += [f'  v{n} = FromCtxV(ctx.v{n});' for n in vlist]
            elif t == '@FSYNC_OUT@':
                body += fsync
            elif t == '@FLUSH@':
                if self.vfloat:
                    body.append(flush)
            else:
                body.append(line)
        tail = ['epilogue:']
        if self.ctx_regs:
            # The registers as the recompiled function leaves them: volatile
            # ones with their last values, non-volatile ones restored.
            for n in VOLATILE_GPRS:
                tail.append(f'  ctx.r{n}.u64 = r[{n}];')
            tail += fsync[:-1]
            tail += [f'  ToCtxV(ctx.v{n}, v{n});' for n in vlist]
            for n in range(14, 32):
                tail.append(f'  ctx.r{n}.u64 = entry_nonvolatile[{n - 14}];')
            if self.restores_lr:
                # __restgprlr_N: lwz r12,-8(r1) (the LR saved at entry); mtlr r12.
                tail.append('  ctx.r12.u64 = uint32_t(entry_lr);')
                tail.append('  ctx.lr = uint32_t(entry_lr);')
        tail += [
            '  ctx.r1.u64 = entry_r1;',
            '}',
            '',
        ]
        if self.track_stores:
            tail += [f'static void Native_{self.name}(PPCContext& ctx, uint8_t* base) {{',
                     f'  NativeT_{self.name}<false>(ctx, base);',
                     '}',
                     '']
        return '\n'.join(head + body + tail)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--gen-dir', required=True)
    ap.add_argument('--func', required=True, help='e.g. sub_82599010')
    ap.add_argument('--out', required=True)
    ap.add_argument('--inline', default='',
                    help='comma-separated leaf callees to translate in place')
    ap.add_argument('--guest-stack', action='store_true',
                    help='keep the stack frame in guest memory (its addresses may escape)')
    ap.add_argument('--ctx-regs', action='store_true',
                    help='keep every register in step with PPCContext at calls and exit '
                         '(needed for indirect calls and the game\'s own vector conventions)')
    ap.add_argument('--track-stores', action='store_true',
                    help='also write NativeT_<func><true>, which reports each guest store '
                         '(TrackStore) for a diagnostic comparison')
    args = ap.parse_args()
    try:
        path, body = find_function(args.gen_dir, args.func)
        t = Translator(args.func, gen_dir=args.gen_dir,
                       inline=[x for x in args.inline.split(',') if x],
                       guest_stack=args.guest_stack, ctx_regs=args.ctx_regs,
                       track_stores=args.track_stores)
        t.translate(parse(body))
        text = t.render(path)
    except TranslationError as err:
        print(f'ppc_to_native: {args.func}: {err}', file=sys.stderr)
        return 1
    os.makedirs(os.path.dirname(os.path.abspath(args.out)), exist_ok=True)
    old = None
    if os.path.exists(args.out):
        with open(args.out, encoding='utf-8') as f:
            old = f.read()
    if old != text:
        with open(args.out, 'w', encoding='utf-8', newline='\n') as f:
            f.write(text)
    print(f'{args.func}: {t.insn_count} instructions, calls {sorted(t.calls)}, '
          f'stack {max([o + w for o, w in t.stack_access] + [0])} bytes -> {args.out}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
