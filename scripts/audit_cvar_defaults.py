"""Compara os padroes das cvars do ReXGlue com as definicoes originais do Xenia
que ficaram comentadas no codigo (`// DEFINE_bool(nome, padrao, ...)`).

Uso: py -3 -I scripts/audit_cvar_defaults.py <rexglue/src>
Lista cada cvar cujo padrao difere (bug 19 foi um caso desses).
"""
import os
import re
import sys

COMMENTED = re.compile(
    r"//\s*DEFINE_(bool|int32|int64|uint32|uint64|double|string)\(\s*(?://\s*)?(\w+)\s*,\s*(?://\s*)?([^,]+?)\s*,",
    re.S)
ACTIVE = re.compile(
    r"REXCVAR_DEFINE_(BOOL|INT32|INT64|UINT32|UINT64|DOUBLE|STRING)\(\s*(\w+)\s*,\s*([^,]+?)\s*,", re.S)


def norm(v):
    return v.strip()


def main():
    root = sys.argv[1]
    original, active = {}, {}
    for d, _, files in os.walk(root):
        for f in files:
            if not f.endswith((".cpp", ".cc", ".h")):
                continue
            p = os.path.join(d, f)
            text = open(p, encoding="utf-8", errors="replace").read()
            # Join comment continuation lines so multi-line commented DEFINEs match.
            joined = re.sub(r"\n\s*//", " //", text)
            for m in COMMENTED.finditer(joined):
                original.setdefault(m.group(2), (norm(m.group(3)), os.path.relpath(p, root)))
            for m in ACTIVE.finditer(text):
                active.setdefault(m.group(2), (norm(m.group(3)), os.path.relpath(p, root)))
    diffs = 0
    for name, (orig, opath) in sorted(original.items()):
        if name not in active:
            continue
        cur, cpath = active[name]
        if cur != orig:
            diffs += 1
            print(f"{name}: Xenia={orig} ReXGlue={cur}  ({cpath})")
    print(f"# {len(original)} definicoes comentadas, {sum(n in active for n in original)} com cvar ativa, {diffs} padroes diferentes")


if __name__ == "__main__":
    main()
