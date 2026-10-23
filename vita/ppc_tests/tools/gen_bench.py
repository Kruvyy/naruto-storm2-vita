"""Generate rexglue/tests/ppc/asm/bench.s with expected values from an
independent Python model of the PowerPC semantics (not from ReXGlue).

The cases are timed by the Vita harness (names "bench.*") and are also normal
correctness tests in every suite.

Usage: py -3 -I gen_bench.py <out bench.s>
"""
import struct
import sys
from fractions import Fraction

M32 = 0xFFFFFFFF
M64 = 0xFFFFFFFFFFFFFFFF


def rotl32(x, n):
    x &= M32
    return ((x << n) | (x >> (32 - n))) & M32


def f32(x):
    """Round a Python float (double) to single precision (nearest-even)."""
    return struct.unpack(">f", struct.pack(">f", x))[0]


def f32_bits(x):
    return struct.unpack(">I", struct.pack(">f", x))[0]


def f32_from_bits(b):
    return struct.unpack(">f", struct.pack(">I", b))[0]


def f32_exact(x: Fraction) -> float:
    """Round an exact rational to single precision, ties to even (normal range)."""
    if x == 0:
        return 0.0
    sign = -1 if x < 0 else 1
    x = abs(x)
    e = x.numerator.bit_length() - x.denominator.bit_length()
    if Fraction(2) ** e > x:
        e -= 1
    scale = Fraction(2) ** (23 - e)
    m = x * scale
    q, r = divmod(m.numerator, m.denominator)
    if 2 * r > m.denominator or (2 * r == m.denominator and q % 2 == 1):
        q += 1
    return sign * float(Fraction(q) / scale)


def bench_int(iters):
    mem = [0] * 64
    r3, r6 = 1, 0
    for _ in range(iters):
        r7 = mem[r6 // 4]                    # lwzx (zero-extended)
        r7 = (r7 + r3) & M64                 # add
        r7 = rotl32(r7, 3)                   # rotlwi (rlwinm ..,0,31): low 32 only
        r3 = (r3 ^ r7) & M64                 # xor
        r3 = (r3 + 0x55) & M64               # addi
        mem[r6 // 4] = r7 & M32              # stwx
        r6 = (r6 + 4) & 0xFC                 # addi + andi.
    return r3, mem


def bench_fp(iters, a, b, c):
    for _ in range(iters):
        f4 = f32(a * b)       # fmuls: exact product of singles fits in double
        c = f32(c + f4)       # fadds: double rounding is innocuous for add
        a = f32(a * b)        # fmuls
    return c, a


def bench_fma(iters, a, b, c):
    for _ in range(iters):
        c = f32_exact(Fraction(a) * Fraction(b) + Fraction(c))   # fmadds: one rounding
        a = f32(a * b)                                           # fmuls
    return c, a


def to_s32(x):
    x &= M32
    return x - (1 << 32) if x & 0x80000000 else x


def struct_init():
    """64 structs {a, b, next, flags}; next is a full-period cycle 5i+1 mod 64."""
    return [[(i * 37 + 11) % 1000 + 1, (i * 53 + 7) % 1000 + 1, (i * 5 + 1) % 64, i] for i in range(64)]


STRUCT_EXECUTED = [0]  # executed PPC instructions, filled by bench_struct


def bench_struct(iters):
    s = struct_init()
    r30, r29 = 0, 0
    executed = 10 + 8                               # prologue + epilogue
    for _ in range(iters):
        a, b = s[r30][0], s[r30][1]                 # lwz (zero-extended)
        if to_s32(a) < to_s32(b):                   # cmpw cr6 + blt
            r10 = (a + b) & M64                     # add
            executed += 13 + 3                      # taken path + leaf
        else:
            r10 = (a - b) & M64                     # subf
            executed += 14 + 3                      # subf + b path + leaf
        s[r30][0] = r10 & M32                       # stw
        r30 = s[r30][2]                             # lwz next
        r3 = rotl32(r10, 1) & 0xFFFFFFFE            # leaf: rlwinm r3,r3,1,0,30
        r3 ^= 0x5A                                  # leaf: xori
        r29 = (r29 + r3) & M64                      # add
    STRUCT_EXECUTED[0] = executed
    return r29, s


VEC_MATRIX = [0.6, -0.5, 0.1, 0.5, 0.6, -0.1, 0.05, 0.1, 0.7, 1.0, -0.5, 0.25]


def vec_points_init():
    return [[f32(((i * 0.37) % 3.0) - 1.5), f32(((i * 0.71) % 2.0) - 1.0), f32(((i * 0.13) % 1.0))]
            for i in range(32)]


def bench_vec3(passes):
    m = [f32(v) for v in VEC_MATRIX]
    pts = vec_points_init()
    F = Fraction

    def fma(a, c, b):  # fmadds: a*c + b, one rounding
        return f32_exact(F(a) * F(c) + F(b))

    def add(a, b):     # fadds
        return f32_exact(F(a) + F(b))

    for _ in range(passes):
        for p in pts:
            x, y, z = p
            nx = add(fma(z, m[2], fma(y, m[1], f32(x * m[0]))), m[9])
            ny = add(fma(z, m[5], fma(y, m[4], f32(x * m[3]))), m[10])
            nz = add(fma(z, m[8], fma(y, m[7], f32(x * m[6]))), m[11])
            p[0], p[1], p[2] = nx, ny, nz
    return m, pts


def hexbytes(words32):
    return " ".join(f"{(w >> s) & 0xFF:02x}" for w in words32 for s in (24, 16, 8, 0))


def main():
    out = sys.argv[1]
    n_int = 8192
    r3, mem = bench_int(n_int)

    a_bits, b_bits, c_bits = 0x3F800347, 0x3F7FF972, 0x3F000000
    a, b, c = f32_from_bits(a_bits), f32_from_bits(b_bits), f32_from_bits(c_bits)
    n_fp = 4096
    c_fp, a_fp = bench_fp(n_fp, a, b, c)
    c_fma, a_fma = bench_fma(n_fp, a, b, c)

    zeros = " ".join(["00"] * 256)
    lines = [
        "# Benchmark + correctness cases for the Vita harness (names bench.*).",
        "# GENERATED by vita/ppc_tests/tools/gen_bench.py from an independent",
        "# Python model of the PPC semantics. Do not edit by hand.",
        "",
        f"# Integer/memory loop, {n_int} iterations x 9 instructions.",
        "test_bench_int:",
        f"  #_ MEMORY_IN 1000D000 {zeros}",
        "  #_ REGISTER_IN r4 0x1000D000",
        "  li r3, 1",
        "  li r6, 0",
        f"  li r5, {n_int}",
        "  mtctr r5",
        "1:",
        "  lwzx r7, r4, r6",
        "  add r7, r7, r3",
        "  rotlwi r7, r7, 3",
        "  xor r3, r3, r7",
        "  addi r3, r3, 0x55",
        "  stwx r7, r4, r6",
        "  addi r6, r6, 4",
        "  andi. r6, r6, 0xFC",
        "  bdnz 1b",
        "  blr",
        f"  #_ REGISTER_OUT r3 0x{r3:016X}",
        f"  #_ MEMORY_OUT 1000D000 {hexbytes(mem[:8])}",
        "",
        f"# Single-precision fmuls/fadds loop, {n_fp} iterations.",
        "test_bench_fp:",
        f"  #_ MEMORY_IN 1000D100 {hexbytes([a_bits, b_bits, c_bits, 0, 0])}",
        "  #_ REGISTER_IN r4 0x1000D100",
        "  lfs f1, 0(r4)",
        "  lfs f2, 4(r4)",
        "  lfs f3, 8(r4)",
        f"  li r5, {n_fp}",
        "  mtctr r5",
        "1:",
        "  fmuls f4, f1, f2",
        "  fadds f3, f3, f4",
        "  fmuls f1, f1, f2",
        "  bdnz 1b",
        "  stfs f3, 12(r4)",
        "  stfs f1, 16(r4)",
        "  blr",
        f"  #_ MEMORY_OUT 1000D10C {hexbytes([f32_bits(c_fp), f32_bits(a_fp)])}",
        "",
        f"# Single-precision fmadds loop (software FMA path on the Vita), {n_fp} iterations.",
        "test_bench_fma:",
        f"  #_ MEMORY_IN 1000D200 {hexbytes([a_bits, b_bits, c_bits, 0, 0])}",
        "  #_ REGISTER_IN r4 0x1000D200",
        "  lfs f1, 0(r4)",
        "  lfs f2, 4(r4)",
        "  lfs f3, 8(r4)",
        f"  li r5, {n_fp}",
        "  mtctr r5",
        "1:",
        "  fmadds f3, f1, f2, f3",
        "  fmuls f1, f1, f2",
        "  bdnz 1b",
        "  stfs f3, 12(r4)",
        "  stfs f1, 16(r4)",
        "  blr",
        f"  #_ MEMORY_OUT 1000D20C {hexbytes([f32_bits(c_fma), f32_bits(a_fma)])}",
        "",
    ]

    # Game-like integer code: register mix of compiled code (non-volatile
    # r29-r31, temporaries r9-r12, cr6, stack frame, lr, a call per step).
    n_struct = 8192
    r29, structs = bench_struct(n_struct)
    s0 = struct_init()
    lines += [
        f"# Game-like integer loop, {n_struct} steps: linked walk over 64 structs of",
        "# {a, b, next, flags}, compare/update, call a leaf function per step.",
        "test_bench_struct:",
        f"  #_ MEMORY_IN 1000D400 {hexbytes([w for st in s0 for w in st])}",
        "  #_ REGISTER_IN r1 0x1000F000",
        "  #_ REGISTER_IN r3 0x1000D400",
        f"  #_ REGISTER_IN r4 {n_struct}",
        "  mflr r12",
        "  stw r12, -8(r1)",
        "  stw r31, -12(r1)",
        "  stw r30, -16(r1)",
        "  stw r29, -20(r1)",
        "  stwu r1, -96(r1)",
        "  mr r31, r3",
        "  li r30, 0",
        "  li r29, 0",
        "  mtctr r4",
        "1:",
        "  rlwinm r11, r30, 4, 0, 27",
        "  add r11, r11, r31",
        "  lwz r10, 0(r11)",
        "  lwz r9, 4(r11)",
        "  cmpw cr6, r10, r9",
        "  blt cr6, 2f",
        "  subf r10, r9, r10",
        "  b 3f",
        "2:",
        "  add r10, r10, r9",
        "3:",
        "  stw r10, 0(r11)",
        "  lwz r30, 8(r11)",
        "  mr r3, r10",
        "  bl test_bench_leaf",
        "  add r29, r29, r3",
        "  bdnz 1b",
        "  mr r3, r29",
        "  addi r1, r1, 96",
        "  lwz r12, -8(r1)",
        "  lwz r31, -12(r1)",
        "  lwz r30, -16(r1)",
        "  lwz r29, -20(r1)",
        "  mtlr r12",
        "  blr",
        f"  #_ REGISTER_OUT r3 0x{r29:016X}",
        f"  #_ MEMORY_OUT 1000D400 {hexbytes([w for st in structs[:4] for w in st])}",
        "",
        "# Leaf called by test_bench_struct (also a test on its own).",
        "test_bench_leaf:",
        "  #_ REGISTER_IN r3 5",
        "  rlwinm r3, r3, 1, 0, 30",
        "  xori r3, r3, 0x5A",
        "  blr",
        f"  #_ REGISTER_OUT r3 0x{((5 << 1) ^ 0x5A):016X}",
        "",
    ]

    # Game-like float code: 3x3 matrix + translation applied to 32 points,
    # matrix kept in non-volatile f14-f25 (saved/restored like compiled code).
    n_pass = 16
    m, pts = bench_vec3(n_pass)
    p0 = vec_points_init()
    # 0(r1) holds the back chain written by stwu; saves start at 8(r1).
    save = [f"  stfd f{14 + k}, {8 + 8 * k}(r1)" for k in range(12)]
    restore = [f"  lfd f{14 + k}, {8 + 8 * k}(r1)" for k in range(12)]
    loads = [f"  lfs f{14 + k}, {4 * k}(r3)" for k in range(12)]
    lines += [
        f"# Game-like float loop: 32 points x {n_pass} passes, p' = M*p + t with fmadds.",
        "test_bench_vec3:",
        f"  #_ MEMORY_IN 1000D800 {hexbytes([f32_bits(v) for v in m])}",
        f"  #_ MEMORY_IN 1000D900 {hexbytes([f32_bits(c) for p in p0 for c in p])}",
        "  #_ REGISTER_IN r1 0x1000F000",
        "  #_ REGISTER_IN r3 0x1000D800",
        "  #_ REGISTER_IN r4 0x1000D900",
        f"  #_ REGISTER_IN r5 {n_pass}",
        "  stwu r1, -112(r1)",
        *save,
        *loads,
        "  mr r10, r5",
        "2:",
        "  li r9, 32",
        "  mtctr r9",
        "  mr r11, r4",
        "1:",
        "  lfs f1, 0(r11)",
        "  lfs f2, 4(r11)",
        "  lfs f3, 8(r11)",
        "  fmuls f4, f1, f14",
        "  fmadds f4, f2, f15, f4",
        "  fmadds f4, f3, f16, f4",
        "  fadds f4, f4, f23",
        "  fmuls f5, f1, f17",
        "  fmadds f5, f2, f18, f5",
        "  fmadds f5, f3, f19, f5",
        "  fadds f5, f5, f24",
        "  fmuls f6, f1, f20",
        "  fmadds f6, f2, f21, f6",
        "  fmadds f6, f3, f22, f6",
        "  fadds f6, f6, f25",
        "  stfs f4, 0(r11)",
        "  stfs f5, 4(r11)",
        "  stfs f6, 8(r11)",
        "  addi r11, r11, 12",
        "  bdnz 1b",
        "  addic. r10, r10, -1",
        "  bne 2b",
        *restore,
        "  addi r1, r1, 112",
        "  blr",
        f"  #_ MEMORY_OUT 1000D900 {hexbytes([f32_bits(c) for p in pts[:4] for c in p])}",
        "",
    ]
    with open(out, "w", encoding="ascii", newline="\n") as f:
        f.write("\n".join(lines))
    print(f"int r3=0x{r3:016X}; fp c={c_fp!r} a={a_fp!r}; fma c={c_fma!r} a={a_fma!r}; "
          f"struct r3=0x{r29:016X}; vec3 p0={pts[0]!r}")
    # Executed PPC instructions per call (for M instr/s in the Vita bench log).
    counts = {
        "int": 4 + n_int * 9 + 1,
        "fp": 5 + n_fp * 4 + 3,
        "fma": 5 + n_fp * 3 + 3,
        "struct": STRUCT_EXECUTED[0],
        "vec3": 26 + n_pass * (3 + 32 * 20 + 2) + 14,
    }
    print("executadas por chamada: " + ", ".join(f"{k}={v}" for k, v in counts.items()))


if __name__ == "__main__":
    main()
