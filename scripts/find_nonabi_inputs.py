"""Lista funcoes geradas que LEEM r0/r2/r11/r12 antes de escreve-los no
primeiro bloco: recebem entrada nesses registros, fora da convencao de chamada
(helpers do tipo __chkstk, __savevmx). Com non_argument_as_local esses valores
ficam em variaveis locais do chamador e o helper le lixo do PPCContext (bug 21).

Aproximacao pelo texto da desmontagem (comentarios "// mnem ops" do codigo
gerado): destino = 1o operando, exceto stores, compares, mt*, tw*, desvios e
dcb*, em que todos os operandos sao lidos.

Uso: py -3 -I scripts/find_nonabi_inputs.py <generated_dir> [regs=r0,r2,r11,r12]
"""
import os
import re
import sys

FUNC_RE = re.compile(r"^DEFINE_REX_FUNC\((\w+)\)")
ASM_RE = re.compile(r"^\t// ([a-z][a-z0-9.+-]*)\s*(.*)$")
REG_RE = re.compile(r"\br(\d+)\b")
ALL_READ = ("st", "cmp", "mt", "tw", "td", "dcb", "icb")


def main():
    gen = sys.argv[1]
    watch = set((sys.argv[2] if len(sys.argv) > 2 else "r0,r2,r11,r12").split(","))
    hits = {}
    for name in sorted(os.listdir(gen)):
        if not name.endswith(".cpp"):
            continue
        func = None
        written = set()
        done = True
        n = 0
        for line in open(os.path.join(gen, name), encoding="utf-8", errors="replace"):
            m = FUNC_RE.match(line)
            if m:
                func, written, done, n = m.group(1), set(), False, 0
                continue
            if done or func is None:
                continue
            if line.startswith("loc_"):
                done = True
                continue
            a = ASM_RE.match(line)
            if not a:
                continue
            mnem, ops = a.group(1), a.group(2)
            n += 1
            regs = ["r" + r for r in REG_RE.findall(ops)]
            if mnem.startswith("b") or mnem.startswith(ALL_READ):
                reads, dest = regs, None
            else:
                dest = regs[0] if regs else None
                reads = regs[1:]
            for r in reads:
                if r in watch and r not in written:
                    hits.setdefault(func, set()).add(r)
            if dest:
                written.add(dest)
            if mnem.startswith("b") or n > 40:
                done = True
    for f in sorted(hits):
        print(f"{f}: le {','.join(sorted(hits[f]))} sem escrever antes")
    print(f"# {len(hits)} funcoes")


if __name__ == "__main__":
    main()
