"""Procura enderecos de funcao montados no codigo (lis rX,hi + addi/ori rY,rX,lo)
que nao viraram funcao no codigo gerado.

Criterios: o par forma um endereco alinhado em secao executavel, precedido por
terminador (blr/bctr/b) ou padding, fora do interior de uma entrada .pdata, e
ausente da tabela de funcoes gerada (<projeto>_init.cpp).

Uso: py -3 -I scripts/find_code_const_functions.py <dump_dir> <generated_dir> [saida.txt]
"""
import bisect
import os
import re
import struct
import sys


def main():
    dump_dir, gen_dir = sys.argv[1], sys.argv[2]
    out_path = sys.argv[3] if len(sys.argv) > 3 else None
    secs = []
    with open(os.path.join(dump_dir, "sections.txt"), encoding="ascii") as f:
        for line in f:
            base, size, x, name, fname = line.split()
            with open(os.path.join(dump_dir, fname), "rb") as g:
                secs.append((int(base, 16), int(size, 16), x == "x", name, g.read()))
    code = [(b, s, d) for b, s, x, _n, d in secs if x]

    def word(a):
        for b, s, d in code:
            if b <= a < b + s - 3:
                return struct.unpack_from(">I", d, a - b)[0]
        return None

    pdata = []
    for b, s, x, n, d in secs:
        if n == ".pdata":
            for off in range(0, len(d) - 7, 8):
                begin, w = struct.unpack_from(">II", d, off)
                if begin:
                    pdata.append((begin, ((w >> 8) & 0x3FFFFF) * 4))
    pdata.sort()
    starts = [p[0] for p in pdata]

    def inside_pdata(a):
        i = bisect.bisect_right(starts, a) - 1
        return i >= 0 and pdata[i][0] < a < pdata[i][0] + pdata[i][1]

    def is_term(w):
        return w is not None and (w == 0 or w in (0x4E800020, 0x4E800420) or
                                  ((w >> 26) == 18 and (w & 1) == 0))

    funcs = set()
    labels = set()
    rx = re.compile(r"^\s*\{\s*0x([0-9A-Fa-f]{8}),\s*[A-Za-z_]\w*\s*\},")
    lab = re.compile(r"^loc_([0-9A-F]{8}):")
    for name in os.listdir(gen_dir):
        if not name.endswith(".cpp"):
            continue
        with open(os.path.join(gen_dir, name), encoding="utf-8", errors="replace") as f:
            for line in f:
                m = rx.match(line)
                if m:
                    funcs.add(int(m.group(1), 16))
                    continue
                m = lab.match(line)
                if m:
                    labels.add(int(m.group(1), 16))

    def feeds_mtctr(d, j, n, reg):
        # Switch pattern: the address register is (added to and) moved to CTR
        # within the next few instructions, then bctr.
        for k in range(j + 1, min(n, j + 6)):
            w = struct.unpack_from(">I", d, k * 4)[0]
            if (w & 0xFC1FFFFF) == 0x7C0903A6:  # mtctr rS
                return True
        return False

    hits = {}
    for b, s, d in code:
        n = s // 4
        for i in range(n):
            w = struct.unpack_from(">I", d, i * 4)[0]
            if (w >> 26) != 15 or ((w >> 16) & 31) != 0:  # addis rD,0,imm == lis
                continue
            rd = (w >> 21) & 31
            hi = (w & 0xFFFF) << 16
            for j in range(i + 1, min(n, i + 9)):
                w2 = struct.unpack_from(">I", d, j * 4)[0]
                op2 = w2 >> 26
                ra2 = (w2 >> 16) & 31
                rt2 = (w2 >> 21) & 31
                if op2 == 14 and ra2 == rd:  # addi
                    lo = w2 & 0xFFFF
                    val = (hi + (lo - 0x10000 if lo & 0x8000 else lo)) & 0xFFFFFFFF
                elif op2 == 24 and rt2 == rd:  # ori rA,rS,uimm (rS field at 21)
                    val = hi | (w2 & 0xFFFF)
                else:
                    if (w2 >> 21) & 31 == rd and op2 not in (36, 37, 38, 44, 45, 47, 54, 55):
                        break  # rd overwritten (rough check; stores use rd as source)
                    continue
                if val & 3 or val in funcs or val in labels or inside_pdata(val):
                    break
                if feeds_mtctr(d, j, n, rt2):
                    break  # computed branch base (switch table), not a function pointer
                if not any(cb <= val < cb + cs for cb, cs, _ in code):
                    break
                if is_term(word(val - 4)):
                    hits.setdefault(val, []).append(b + i * 4)
                break
    lines = [f"{v:08X} refs={len(r)} primeiro_ref={r[0]:08X}" for v, r in sorted(hits.items())]
    print(f"funcoes geradas: {len(funcs)}; enderecos montados no codigo sem funcao: {len(lines)}")
    for l in lines[:40]:
        print(l)
    if out_path:
        with open(out_path, "w", encoding="ascii") as f:
            f.write("\n".join(lines) + "\n")


if __name__ == "__main__":
    main()
