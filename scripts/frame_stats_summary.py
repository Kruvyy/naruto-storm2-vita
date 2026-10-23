"""Resumo do frame_stats.csv gerado com --perf_frame_stats.

Uso: py -3 -I scripts/frame_stats_summary.py <frame_stats.csv> [inicio_quadro] [fim_quadro]
Mostra quadros, tempo, instrucoes do convidado por quadro (percentis) e memoria.
"""
import csv
import statistics
import sys


def pct(sorted_vals, p):
    if not sorted_vals:
        return 0
    k = min(len(sorted_vals) - 1, max(0, int(round(p / 100.0 * (len(sorted_vals) - 1)))))
    return sorted_vals[k]


def main():
    path = sys.argv[1]
    lo = int(sys.argv[2]) if len(sys.argv) > 2 else 2
    hi = int(sys.argv[3]) if len(sys.argv) > 3 else 10**9
    rows = list(csv.DictReader(open(path, encoding="ascii")))
    sel = [r for r in rows if lo <= int(r["frame"]) <= hi]
    ins = sorted(int(r["guest_insns_frame"]) for r in sel)
    fus = sorted(int(r["frame_us"]) for r in sel)
    mem = [r for r in rows if r["virtual_committed"]]
    print(f"quadros no arquivo: {len(rows)} (analisados {lo}..{min(hi, len(rows))}: {len(sel)})")
    if sel:
        dur = (int(sel[-1]["host_us"]) - int(sel[0]["host_us"])) / 1e6
        print(f"tempo: {dur:.1f} s -> {len(sel) / dur if dur else 0:.1f} quadros/s no PC")
        print("instrucoes do convidado por quadro: "
              f"media {statistics.mean(ins):,.0f}; p50 {pct(ins, 50):,}; p90 {pct(ins, 90):,}; "
              f"p99 {pct(ins, 99):,}; max {ins[-1]:,}")
        print(f"tempo de quadro no PC (us): p50 {pct(fus, 50):,}; p90 {pct(fus, 90):,}")
        total = int(sel[-1]["guest_insns_total"]) - int(sel[0]["guest_insns_total"])
        print(f"instrucoes no intervalo: {total:,} -> {total / dur / 1e6 if dur else 0:,.1f} M/s no PC")
    if mem:
        last = mem[-1]
        mb = lambda v: int(v) / (1024 * 1024)
        print("memoria do convidado (ultimo amostrado, MiB): "
              f"virtual comprometida {mb(last['virtual_committed']):.1f} "
              f"(reservada {mb(last['virtual_reserved']):.1f}); "
              f"fisica comprometida {mb(last['physical_committed']):.1f} "
              f"(reservada {mb(last['physical_reserved']):.1f})")
        print("pico (MiB): "
              f"virtual {mb(last['peak_virtual_committed']):.1f}; "
              f"fisica {mb(last['peak_physical_committed']):.1f}; "
              f"soma {mb(last['peak_virtual_committed']) + mb(last['peak_physical_committed']):.1f}")


if __name__ == "__main__":
    main()
