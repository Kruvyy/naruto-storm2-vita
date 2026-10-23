"""Linha do tempo de frame_stats.csv em janelas de N segundos: instrucoes por
quadro (mediana e p90) e memoria comprometida, para separar fases (menu,
carregamento, batalha).

Uso: py -3 -I scripts/frame_timeline.py <frame_stats.csv> [janela_s=15]
"""
import csv
import sys


def pct(values, p):
    s = sorted(values)
    return s[min(len(s) - 1, int(p * (len(s) - 1)))]


def main():
    rows = list(csv.DictReader(open(sys.argv[1], encoding="latin-1")))
    win = float(sys.argv[2]) if len(sys.argv) > 2 else 15.0
    t0 = int(rows[1]["host_us"])
    buckets = {}
    last_mem = {}
    for r in rows[1:]:
        t = (int(r["host_us"]) - t0) / 1e6
        b = int(t // win)
        buckets.setdefault(b, []).append(int(r["guest_insns_frame"]))
        if r["physical_committed"]:
            last_mem[b] = (float(r["virtual_committed"]) / 2**20, float(r["physical_committed"]) / 2**20)
    print(f"{'inicio_s':>8} {'quadros':>7} {'p50_M':>7} {'p90_M':>7} {'virt_MiB':>8} {'fis_MiB':>8}")
    mem = (0.0, 0.0)
    for b in sorted(buckets):
        v = buckets[b]
        mem = last_mem.get(b, mem)
        print(f"{b * win:8.0f} {len(v):7d} {pct(v, 0.5) / 1e6:7.1f} {pct(v, 0.9) / 1e6:7.1f} {mem[0]:8.1f} {mem[1]:8.1f}")


if __name__ == "__main__":
    main()
