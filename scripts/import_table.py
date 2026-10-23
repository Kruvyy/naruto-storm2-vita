"""Tabela de imports do Storm 2: estado estatico no ReXGlue + uso em execucao.

Uso: py -3 -I scripts/import_table.py <storm2_init.cpp> <rexglue/src> <saida.md> [import_stats.csv]

Estado estatico (codigo do ReXGlue):
  impl        REX_EXPORT(__imp__X, ...)      implementacao real (a verificar caso a caso)
  STUB        REX_EXPORT_STUB(__imp__X)      so registra aviso; NAO faz nada e nao define r3
  STUB_RET    REX_EXPORT_STUB_RETURN(...)    so devolve um valor fixo
  AUSENTE     nenhuma definicao encontrada
Uso em execucao (opcional): import_stats.csv gerado com --perf_frame_stats.
"""
import csv
import os
import re
import sys

MAP = re.compile(r"\{\s*0x([0-9A-Fa-f]{8}),\s*(__imp__\w+)\s*\}")
DEFS = [
    ("impl", re.compile(r"\bREX_EXPORT\(\s*(__imp__\w+)\s*,")),
    ("STUB_RET", re.compile(r"\bREX_EXPORT_STUB_RETURN\(\s*(__imp__\w+)\s*,\s*([^)]+)\)")),
    ("STUB", re.compile(r"\bREX_EXPORT_STUB\(\s*(__imp__\w+)\s*\)")),
    ("impl", re.compile(r"\bREX_HOOK_RAW\(\s*(__imp__\w+)\s*\)")),
    ("impl", re.compile(r"\bREX_HOOK\(\s*(__imp__\w+)\s*,")),
]


def main():
    init_cpp, src_dir, out_md = sys.argv[1:4]
    stats_csv = sys.argv[4] if len(sys.argv) > 4 else None

    imports = {}
    with open(init_cpp, encoding="utf-8", errors="replace") as f:
        for line in f:
            m = MAP.search(line)
            if m:
                imports[m.group(2)] = int(m.group(1), 16)

    defs = {}
    for root, _dirs, files in os.walk(src_dir):
        for fn in files:
            if not fn.endswith((".cpp", ".h", ".inc")):
                continue
            path = os.path.join(root, fn)
            with open(path, encoding="utf-8", errors="replace") as f:
                text = f.read()
            for kind, rx in DEFS:
                for m in rx.finditer(text):
                    name = m.group(1)
                    if name in defs and defs[name][0] == "impl":
                        continue
                    extra = m.group(2).strip() if kind == "STUB_RET" else ""
                    defs[name] = (kind, os.path.relpath(path, src_dir).replace("\\", "/"), extra)

    stats = {}
    if stats_csv and os.path.exists(stats_csv):
        with open(stats_csv, encoding="utf-8") as f:
            for row in csv.DictReader(f):
                stats["__imp__" + row["name"].removeprefix("__imp__")] = row

    rows = []
    for name, addr in sorted(imports.items(), key=lambda kv: kv[0].lower()):
        kind, where, extra = defs.get(name, ("AUSENTE", "", ""))
        st = stats.get(name)
        calls = int(st["calls"]) if st else 0
        first = st["first_frame"] if st else ""
        rows.append((name, addr, kind, where, extra, calls, first))

    counts = {}
    for r in rows:
        counts[r[2]] = counts.get(r[2], 0) + 1
    used = [r for r in rows if r[5] > 0]
    used_stub = [r for r in used if r[2] != "impl"]

    with open(out_md, "w", encoding="utf-8") as out:
        out.write("# Imports do Storm 2\n\n")
        out.write(f"Importados (thunks no XEX): **{len(rows)}**. Estado estatico no ReXGlue: ")
        out.write(", ".join(f"{k}={v}" for k, v in sorted(counts.items())) + ".\n\n")
        if stats:
            out.write(f"Execucao medida: **{len(used)}** imports chamados; "
                      f"**{len(used_stub)}** deles sao STUB/STUB_RET/AUSENTE (bloqueios).\n\n")
        else:
            out.write("Execucao: ainda nao medida (rodar com --perf_frame_stats).\n\n")
        out.write("Colunas do briefing: identificado = todos; necessario = chamado na execucao "
                  "medida; implementado = impl (nao implica correto); testado = sem teste "
                  "dedicado ainda; bloqueado = chamado e STUB/AUSENTE.\n\n")
        out.write("| import | thunk | estado | fonte | chamadas | 1o quadro | bloqueado |\n")
        out.write("|---|---|---|---|---:|---:|---|\n")
        for name, addr, kind, where, extra, calls, first in rows:
            blocked = "**sim**" if calls > 0 and kind != "impl" else ""
            kind_txt = f"{kind} ({extra})" if extra else kind
            out.write(f"| `{name[7:]}` | `0x{addr:08X}` | {kind_txt} | {where} | "
                      f"{calls if stats else ''} | {first} | {blocked} |\n")
    print(f"imports={len(rows)} {counts} usados={len(used)} usados_nao_impl={len(used_stub)}")


if __name__ == "__main__":
    main()
