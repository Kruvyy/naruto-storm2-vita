"""Histograma de mnemonicos PPC no C++ gerado pelo ReXGlue.

O gerador escreve cada instrucao original como comentario "\t// <mnemonico> ...".
Uso: py -3 -I scripts/insn_histogram.py <generated_dir> <ppc_test_functions.cpp> <saida.md>
"""
import collections
import os
import re
import sys

LINE = re.compile(r"^\t// ([a-z][a-z0-9.+\-]*)(?:\s|$)")
UNIMPL = re.compile(r"// UNIMPLEMENTED: (\S+)")


def main():
    gen_dir, asm_dir, out_path = sys.argv[1:4]
    counts = collections.Counter()
    unimpl = collections.Counter()
    for name in os.listdir(gen_dir):
        if not name.endswith(".cpp"):
            continue
        with open(os.path.join(gen_dir, name), encoding="utf-8", errors="replace") as f:
            for line in f:
                m = UNIMPL.search(line)
                if m:
                    unimpl[m.group(1)] += 1
                    continue
                m = LINE.match(line)
                if m:
                    counts[m.group(1)] += 1

    # Coverage = mnemonics as DECODED in the generated test code (the decoder
    # picks aliases such as clrlwi/rotlwi from operand patterns, and aliases
    # have their own builders), not the names written in the .s sources.
    tested = set()
    with open(asm_dir, encoding="utf-8", errors="replace") as f:
        for line in f:
            m = LINE.match(line)
            if m:
                tested.add(m.group(1))

    def base(m):
        return m.rstrip("+-")

    total = sum(counts.values())
    rows = sorted(counts.items(), key=lambda kv: -kv[1])
    with open(out_path, "w", encoding="utf-8") as out:
        out.write("# Histograma de instrucoes do Storm 2 (C++ gerado)\n\n")
        out.write(f"Total de instrucoes: {total}. Mnemonicos distintos: {len(counts)}.\n\n")
        out.write("## Nao implementadas no ReXGlue (geram REX_UNIMPLEMENTED)\n\n")
        if unimpl:
            for k, v in unimpl.most_common():
                out.write(f"- `{k}`: {v} ocorrencias\n")
        else:
            out.write("Nenhuma.\n")
        out.write("\n## Uso por mnemonico\n\n| mnemonico | ocorrencias | % | coberto por teste .s |\n|---|---:|---:|---|\n")
        for k, v in rows:
            cov = "sim" if base(k) in tested or k in tested else "**nao**"
            out.write(f"| `{k}` | {v} | {100.0 * v / total:.3f} | {cov} |\n")
    print(f"total={total} distintos={len(counts)} nao_implementadas={dict(unimpl)}")
    untested = [(k, v) for k, v in rows if not (base(k) in tested or k in tested)]
    un_total = sum(v for _, v in untested)
    print(f"mnemonicos sem teste: {len(untested)} de {len(rows)}; "
          f"ocorrencias sem teste: {un_total} de {total} ({100.0 * un_total / total:.2f}%)")
    print("sem teste (top 60):", ", ".join(f"{k}:{v}" for k, v in untested[:60]))


if __name__ == "__main__":
    main()
