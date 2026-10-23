"""Audita os switch emitidos para bctr no C++ gerado (bug 20).

Para cada `switch (ctx.rN.u32)` emitido num bctr, olha para tras ate o lwzx/lbzx/lhzx
que alimentou o ctr e classifica:
  - indice_e_tabela: o registro do switch tambem recebe lis/addi (endereco da
    tabela) antes do load, ou seja, o switch usa o endereco, nao o indice
    (operandos RA/RB trocados no detector);
  - so_funcoes: todos os cases sao chamadas de funcao (tabela de despacho);
  - so_um_case: switch com um unico case.

Uso: py -3 -I scripts/audit_switch_tables.py <generated_dir> [--list]
"""
import os
import re
import sys

FUNC_RE = re.compile(r"^DEFINE_REX_FUNC\((sub_[0-9A-F]+)\)")
# ctx.rN, or a local rN when the register-as-local codegen options are on.
SWITCH_RE = re.compile(r"^\tswitch \((?:ctx\.)?r(\d+)\.u32\) \{")
CASE_RE = re.compile(r"^\tcase \d+:")
LOAD_RE = re.compile(r"^\t// (lwzx|lbzx|lhzx) r(\d+),r(\d+),r(\d+)")
CONST_RE = re.compile(r"^\t// (lis|addi) r(\d+),")


def main():
    gen = sys.argv[1]
    show = "--list" in sys.argv
    total = swapped = only_funcs = single = 0
    listed = []
    for name in sorted(os.listdir(gen)):
        if not name.endswith(".cpp"):
            continue
        lines = open(os.path.join(gen, name), encoding="utf-8", errors="replace").read().split("\n")
        func = None
        for i, line in enumerate(lines):
            m = FUNC_RE.match(line)
            if m:
                func = m.group(1)
                continue
            m = SWITCH_RE.match(line)
            if not m:
                continue
            total += 1
            reg = int(m.group(1))
            # Find the load that fed ctr, then look at the straight-line code
            # before it (up to a branch or label): if the load's RB register is
            # built with lis/addi, RB holds the table address and RA the index,
            # the order the detector does not expect.
            is_swapped = False
            load_rb = None
            for j in range(i - 1, max(0, i - 60), -1):
                if FUNC_RE.match(lines[j]):
                    break
                lm = LOAD_RE.match(lines[j])
                if lm and load_rb is None:
                    load_rb = int(lm.group(4))
                    continue
                if load_rb is None:
                    continue
                if lines[j].startswith("loc_") or re.match(r"^\t// b", lines[j]):
                    break
                cm = CONST_RE.match(lines[j])
                if cm and int(cm.group(2)) == load_rb:
                    is_swapped = True
                    break
            # Collect cases.
            cases, funcs = 0, 0
            k = i + 1
            while k < len(lines) and not lines[k].startswith("\t}"):
                if CASE_RE.match(lines[k]):
                    cases += 1
                    if re.match(r"^\t\tsub_[0-9A-F]+\(ctx, base\);", lines[k + 1]):
                        funcs += 1
                k += 1
            swapped += is_swapped
            only_funcs += cases > 0 and funcs == cases
            single += cases == 1
            if show and (is_swapped or (cases > 0 and funcs == cases)):
                listed.append(f"{func}: r{reg} cases={cases} funcoes={funcs} indice_e_tabela={is_swapped}")
    for l in listed:
        print(l)
    print(f"switches: {total}; indice_e_tabela: {swapped}; so_funcoes: {only_funcs}; so_um_case: {single}")


if __name__ == "__main__":
    main()
