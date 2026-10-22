"""Consulta a tabela .pdata (Xbox 360, entradas de 8 bytes big-endian).

Uso: py -3 -I scripts/pdata_query.py <dump_dir> <endereco_hex> [...]
Cada entrada: BeginAddress (u32) e um u32 com PrologLength:8, FunctionLength:22
(em palavras de 4 bytes), Flag32Bit:1, ExceptionFlag:1.
"""
import bisect
import os
import struct
import sys


def load(dump_dir):
    with open(os.path.join(dump_dir, "sections.txt"), encoding="ascii") as f:
        for line in f:
            base, size, _x, name, fname = line.split()
            if name == ".pdata":
                with open(os.path.join(dump_dir, fname), "rb") as g:
                    data = g.read()
                break
        else:
            raise SystemExit(".pdata nao encontrada")
    entries = []
    for off in range(0, len(data) - 7, 8):
        begin, word = struct.unpack_from(">II", data, off)
        if begin == 0:
            continue
        length = ((word >> 8) & 0x3FFFFF) * 4
        entries.append((begin, length))
    entries.sort()
    return entries


def main():
    entries = load(sys.argv[1])
    starts = [e[0] for e in entries]
    print(f"entradas .pdata: {len(entries)}")
    for a in sys.argv[2:]:
        addr = int(a, 16)
        i = bisect.bisect_right(starts, addr) - 1
        prev = entries[i] if i >= 0 else None
        nxt = entries[i + 1] if i + 1 < len(entries) else None
        inside = prev and addr < prev[0] + prev[1]
        exact = prev and prev[0] == addr
        print(
            f"{addr:08X}: {'INICIO de entrada' if exact else ('dentro de' if inside else 'FORA; anterior')} "
            f"{prev[0]:08X}+0x{prev[1]:X} (fim {prev[0] + prev[1]:08X}); proxima {nxt[0]:08X}"
        )


if __name__ == "__main__":
    main()
