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
  - guest instruction counting is dropped.
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
    m = re.fullmatch(r'r(\d+)', tok)
    if not m:
        raise TranslationError(f'expected a GPR, got {tok}')
    return int(m.group(1))


def fpr(tok):
    m = re.fullmatch(r'f(\d+)', tok)
    if not m:
        raise TranslationError(f'expected an FPR, got {tok}')
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


def target_label(tok):
    m = re.fullmatch(r'0x([0-9A-Fa-f]{8})', tok)
    if not m:
        raise TranslationError(f'expected a branch target, got {tok}')
    return 'L_' + m.group(1).upper()


class Translator:
    def __init__(self, name):
        self.name = name
        self.out = []
        self.frame = None          # stack frame size once stwu ran
        self.stack_access = set()  # (offset, width)
        self.cr_used = set()
        self.calls = set()
        self.labels = set()
        self.targets = set()
        self.insn_count = 0

    def emit(self, text):
        self.out.append('  ' + text)

    def ea(self, d, ra):
        return f'(r[{ra}] + {d}u)' if ra else f'{d & 0xFFFFFFFF}u'

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
        for n in [0] + list(range(3, 13)):
            self.emit(f'ctx.r{n}.u64 = r[{n}];')
        for n in range(0, 14):
            self.emit(f'ctx.f{n}.f64 = double(f[{n}]);')

    def call_sync_in(self):
        for n in [0] + list(range(3, 13)):
            self.emit(f'r[{n}] = ctx.r{n}.u32;')
        for n in range(0, 14):
            self.emit(f'f[{n}] = float(ctx.f{n}.f64);')

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
                    e('goto epilogue;')
                return  # save/restore helpers: not needed (see the header)
            if op == 'b':
                t = target_label(a[0])
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
            self.calls.add(callee)
            self.call_sync_out()
            e(f'{callee}(ctx, base);')
            self.call_sync_in()
            return
        if op == 'stwu':
            rs, (d, ra) = gpr(a[0]), mem(a[1])
            if ra != 1 or rs != 1 or d >= 0:
                raise TranslationError(f'unsupported stwu form: {text}')
            if self.frame is not None:
                raise TranslationError('second stwu')
            self.frame = -d
            e(f'r[1] -= {-d}u;  // frame of {-d} bytes (local array; back chain not kept)')
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
            if ra == 1:
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
                e(f'Store32(base, {addr}, r[{gpr(reg)}]);')
            elif op == 'lfs':
                e(f'f[{fpr(reg)}] = LoadF(base, {addr});')
            else:
                e(f'StoreF(base, {addr}, f[{fpr(reg)}]);')
            return
        if op in ('lfsx', 'stfsx'):
            fr, ra, rb = fpr(a[0]), gpr(a[1]), gpr(a[2])
            if ra == 1 or rb == 1:
                raise TranslationError(f'indexed stack access: {text}')
            addr = f'(r[{ra}] + r[{rb}])' if ra else f'r[{rb}]'
            if op == 'lfsx':
                e(f'f[{fr}] = LoadF(base, {addr});')
            else:
                e(f'StoreF(base, {addr}, f[{fr}]);')
            return
        if op == 'fmr':
            e(f'f[{fpr(a[0])}] = f[{fpr(a[1])}];')
            return
        if op in ('fadds', 'fsubs', 'fmuls'):
            fd, fa, fb = fpr(a[0]), fpr(a[1]), fpr(a[2])
            sym = {'fadds': '+', 'fsubs': '-', 'fmuls': '*'}[op]
            e(f'f[{fd}] = f[{fa}] {sym} f[{fb}];')
            return
        if op == 'fmadds':
            fd, fa, fc, fb = fpr(a[0]), fpr(a[1]), fpr(a[2]), fpr(a[3])
            e(f'f[{fd}] = float(double(f[{fa}]) * double(f[{fc}]) + double(f[{fb}]));')
            return
        if op == 'mtctr':
            e(f'ctr = r[{gpr(a[0])}];')
            return
        if op == 'bdnz':
            t = target_label(a[0])
            self.targets.add(t)
            e(f'if (--ctr != 0) goto {t};')
            return
        if op in ('blt', 'bgt', 'beq', 'bge', 'ble', 'bne'):
            if len(a) == 1:
                field, t = 0, target_label(a[0])
            else:
                field, t = crf(a[0]), target_label(a[1])
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

    def translate(self, items):
        for item in items:
            if item[0] == 'label':
                label = 'L_' + item[1]
                self.labels.add(label)
                self.out.append(f'{label}:;')
            else:
                try:
                    self.insn(item[1], item[2])
                except TranslationError as err:
                    raise TranslationError(f'{item[1]}: {err}')
        missing = self.targets - self.labels
        if missing:
            raise TranslationError(f'branch targets without a label: {sorted(missing)}')
        self.check_stack()

    def render(self, source_path):
        stack_size = max([o + w for o, w in self.stack_access] + [8])
        stack_size = (stack_size + 15) & ~15
        head = [
            f'// Native version of {self.name}, written by scripts/ppc_to_native.py from',
            f'// {os.path.basename(source_path)} ({self.insn_count} guest instructions).',
            '// Derived from the game\'s code: never commit.',
            '',
        ]
        for callee in sorted(self.calls):
            head.append(f'REX_EXTERN({callee});')
        head += [
            '',
            f'static void Native_{self.name}(PPCContext& ctx, uint8_t* base) {{',
            '  using namespace storm2_native;',
            '  uint32_t r[32] = {};',
            '  float f[32] = {};',
            '  uint32_t ctr = 0;',
        ]
        for field in sorted(self.cr_used):
            head.append(f'  bool cr{field}_lt = false, cr{field}_gt = false, cr{field}_eq = false;')
        head += [
            f'  alignas(8) uint8_t stack[{stack_size}];',
            '  const uint64_t entry_r1 = ctx.r1.u64;',
            '  r[1] = ctx.r1.u32;',
            '  r[13] = ctx.r13.u32;',
        ]
        for n in range(3, 11):
            head.append(f'  r[{n}] = ctx.r{n}.u32;')
        for n in range(1, 9):
            head.append(f'  f[{n}] = float(ctx.f{n}.f64);')
        head.append('  ctx.fpscr.disableFlushMode();')
        tail = [
            'epilogue:',
            '  ctx.r1.u64 = entry_r1;',
            '}',
            '',
        ]
        return '\n'.join(head + self.out + tail)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--gen-dir', required=True)
    ap.add_argument('--func', required=True, help='e.g. sub_82599010')
    ap.add_argument('--out', required=True)
    args = ap.parse_args()
    try:
        path, body = find_function(args.gen_dir, args.func)
        t = Translator(args.func)
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
