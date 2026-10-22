"""Procura possiveis funcoes alcancadas so por ponteiros em dados e nao geradas.

Criterios (todos):
  * palavra de 32 bits big-endian, alinhada, em secao NAO executavel;
  * aponta para secao executavel, endereco alinhado a 4;
  * o alvo NAO e inicio de funcao gerada (DEFINE_REX_FUNC no C++);
  * o alvo NAO esta dentro de uma entrada .pdata (meio de funcao conhecida),
    exceto se for exatamente o inicio dela;
  * a palavra anterior ao alvo e um terminador (blr/b/bctr, ou 0 de padding),
    o que indica inicio de rotina e nao meio de bloco.

Uso: py -3 -I scripts/find_data_only_functions.py <dump_dir> <generated_dir> [saida.txt]
"""
import bisect
import os
import re
import struct
import sys


def sections(dump_dir):
    out = []
    with open(os.path.join(dump_dir, "sections.txt"), encoding="ascii") as f:
        for line in f:
            base, size, x, name, fname = line.split()
            with open(os.path.join(dump_dir, fname), "rb") as g:
                out.append((int(base, 16), int(size, 16), x == "x", name, g.read()))
    return out


def word_at(secs, addr):
    for base, size, _x, _n, data in secs:
        if base <= addr < base + size - 3:
            return struct.unpack_from(">I", data, addr - base)[0]
    return None


def is_terminator(w):
    if w is None:
        return False
    if w == 0:
        return True
    op = w >> 26
    if w == 0x4E800020 or w == 0x4E800420:  # blr, bctr
        return True
    if op == 18 and (w & 1) == 0:  # b (not bl)
        return True
    return False


def main():
    dump_dir, gen_dir = sys.argv[1], sys.argv[2]
    out_path = sys.argv[3] if len(sys.argv) > 3 else None
    secs = sections(dump_dir)
    code = [(b, s) for b, s, x, _n, _d in secs if x]

    pdata = []
    for b, s, x, n, d in secs:
        if n == ".pdata":
            for off in range(0, len(d) - 7, 8):
                begin, w = struct.unpack_from(">II", d, off)
                if begin:
                    pdata.append((begin, ((w >> 8) & 0x3FFFFF) * 4))
    pdata.sort()
    starts = [p[0] for p in pdata]

    # Registered functions = the address->function table in <project>_init.cpp
    # ({ 0xADDR, name }), which also covers helpers such as __savegprlr_14.
    funcs = set()
    mapping = re.compile(r"^\s*\{\s*0x([0-9A-Fa-f]{8}),\s*[A-Za-z_]\w*\s*\},")
    labels = set()
    lab = re.compile(r"^loc_([0-9A-F]{8}):")
    for name in os.listdir(gen_dir):
        if name.endswith(".cpp"):
            with open(os.path.join(gen_dir, name), encoding="utf-8", errors="replace") as f:
                for line in f:
                    m = mapping.match(line)
                    if m:
                        funcs.add(int(m.group(1), 16))
                        continue
                    m = lab.match(line)
                    if m:
                        labels.add(int(m.group(1), 16))

    def in_code(a):
        return any(b <= a < b + s for b, s in code)

    def inside_pdata(a):
        i = bisect.bisect_right(starts, a) - 1
        return i >= 0 and pdata[i][0] < a < pdata[i][0] + pdata[i][1]

    hits = {}
    for b, s, x, n, d in secs:
        if x:
            continue
        for off in range(0, len(d) - 3, 4):
            v = struct.unpack_from(">I", d, off)[0]
            if v & 3 or not in_code(v) or v in funcs or inside_pdata(v):
                continue
            if not is_terminator(word_at(secs, v - 4)):
                continue
            hits.setdefault(v, []).append(b + off)

    lines = []
    for v in sorted(hits):
        refs = hits[v]
        status = "label-interno" if v in labels else "AUSENTE"
        lines.append(f"{v:08X} {status} refs={len(refs)} primeiro_ref={refs[0]:08X}")
    missing = [l for l in lines if "AUSENTE" in l]
    print(f"funcoes geradas: {len(funcs)}; candidatos: {len(lines)}; sem codigo gerado: {len(missing)}")
    for l in lines[:80]:
        print(l)
    if out_path:
        with open(out_path, "w", encoding="ascii") as f:
            f.write("\n".join(lines) + "\n")


if __name__ == "__main__":
    main()
