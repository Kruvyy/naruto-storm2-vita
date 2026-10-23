"""Soma os perfis de profile_session.ps1 (perfil_<thread>_cNNN.csv) de um
conjunto de ciclos e mostra a concentracao do tempo no codigo do jogo.

Uso: py -3 -I scripts/aggregate_profiles.py <run_dir> <thread_tag> <ciclos> [top=20]
  ciclos: "5-12" ou "5,6,9" ou "all"
Saida: funcoes do jogo por fatia do tempo no codigo do jogo, com acumulado, e
quantas funcoes cobrem 50/80/90%.
"""
import csv
import glob
import os
import re
import sys


def parse_cycles(spec, available):
    if spec == "all":
        return sorted(available)
    out = set()
    for part in spec.split(","):
        if "-" in part:
            a, b = part.split("-")
            out.update(range(int(a), int(b) + 1))
        else:
            out.add(int(part))
    return sorted(c for c in out if c in available)


def main():
    run, tag, spec = sys.argv[1], sys.argv[2], sys.argv[3]
    top = int(sys.argv[4]) if len(sys.argv) > 4 else 20
    files = {}
    for f in glob.glob(os.path.join(run, f"perfil_{tag}_c*.csv")):
        m = re.search(r"_c(\d+)\.csv$", f)
        files[int(m.group(1))] = f
    cycles = parse_cycles(spec, files.keys())
    game, other = {}, {}
    for c in cycles:
        for row in csv.DictReader(open(files[c], encoding="latin-1")):
            n = int(row["amostras"])
            d = game if row["simbolo"].startswith("sub_") else other
            d[row["simbolo"]] = d.get(row["simbolo"], 0) + n
    gsum = sum(game.values())
    osum = sum(other.values())
    print(f"ciclos {cycles[0]}..{cycles[-1]} ({len(cycles)}); amostras: jogo {gsum}, fora do jogo {osum}"
          f" ({100.0 * gsum / max(1, gsum + osum):.1f}% no jogo)")
    acc = 0
    marks = {}
    for i, (name, n) in enumerate(sorted(game.items(), key=lambda kv: -kv[1]), 1):
        acc += n
        share = 100.0 * acc / gsum
        for m in (50, 80, 90):
            if m not in marks and share >= m:
                marks[m] = i
        if i <= top:
            print(f"{i:3d} {name}  {100.0 * n / gsum:5.1f}%  acumulado {share:5.1f}%")
    print("funcoes para cobrir 50/80/90% do tempo no jogo: "
          + ", ".join(f"{m}%={marks.get(m, '-')}" for m in (50, 80, 90))
          + f" (de {len(game)} funcoes vistas)")


if __name__ == "__main__":
    main()
