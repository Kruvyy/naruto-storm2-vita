"""Desmonta um intervalo de enderecos do convidado a partir de um dump de secoes.

Uso: py -3 -I scripts/dis.py <dump_dir> <inicio_hex> <bytes_hex_ou_dec>
Usa o objdump PowerPC (Cygwin) incluido no ReXGlue.
"""
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OBJDUMP = os.path.join(ROOT, "rexglue", "tools", "binutils", "powerpc-none-elf-objdump.exe")


def find_section(dump_dir, addr):
    with open(os.path.join(dump_dir, "sections.txt"), encoding="ascii") as f:
        for line in f:
            base, size, _x, name, fname = line.split()
            base, size = int(base, 16), int(size, 16)
            if base <= addr < base + size:
                return base, os.path.join(dump_dir, fname)
    raise SystemExit(f"{addr:08X} fora das secoes")


def main():
    dump_dir, start, length = sys.argv[1], int(sys.argv[2], 16), int(sys.argv[3], 0)
    base, path = find_section(dump_dir, start)
    rel = os.path.relpath(path).replace("\\", "/")
    env = dict(os.environ, CYGWIN="nodosfilewarning")
    out = subprocess.run(
        [OBJDUMP, "-b", "binary", "-m", "powerpc:common64", "-EB", "-D",
         f"--start-address={start - base:#x}", f"--stop-address={start - base + length:#x}", rel],
        capture_output=True, text=True, env=env, check=True).stdout
    for line in out.splitlines():
        head, sep, rest = line.partition(":")
        if sep and head.strip() and all(c in "0123456789abcdef" for c in head.strip()):
            print(f"{int(head.strip(), 16) + base:08X}:{rest}")


if __name__ == "__main__":
    main()
