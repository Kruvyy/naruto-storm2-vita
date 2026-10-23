"""Resumo de thread_insns.csv: instrucoes do convidado por thread.

Uso: py -3 -I scripts/thread_insns_summary.py <thread_insns.csv> [n]
"""
import csv
import sys


def main():
    rows = list(csv.DictReader(open(sys.argv[1], encoding="latin-1")))
    n = int(sys.argv[2]) if len(sys.argv) > 2 else 15
    total = sum(int(r["guest_insns"]) for r in rows)
    rows.sort(key=lambda r: -int(r["guest_insns"]))
    print(f"dump no quadro {rows[0]['frame']}; threads: {len(rows)}; total: {total:,}")
    for r in rows[:n]:
        v = int(r["guest_insns"])
        print(f"{r['native_thread']:>8} {v:>18,} {100.0 * v / total:5.1f}%  {r['guest_thread_name']}")


if __name__ == "__main__":
    main()
