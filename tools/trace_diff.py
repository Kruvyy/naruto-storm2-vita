"""Compares two GLES traces of one frame (gles_trace_swap): the sequence of
draws (render target, pixel shader, primitive count) and resolves, PC vs Vita.

usage: py -3 -I tools/trace_diff.py <log A> <log B>
"""
import difflib
import re
import sys


def events(path):
    out = []
    for line in open(path, encoding="utf-8", errors="replace"):
        i = line.find("GLES trace: ")
        if i < 0:
            continue
        body = line[i + len("GLES trace: "):].strip()
        if body.startswith("va") or body.startswith("  "):
            continue
        m = re.match(r"draw prim (\d+) count (\d+) rt color (\S+) fmt (\d+).*?ps ([0-9A-F]+)", body)
        if m:
            out.append(f"draw rt {m.group(3)} fmt {m.group(4)} ps {m.group(5)[:8]} n {m.group(2)}")
            continue
        out.append(re.sub(r"\s+", " ", body)[:120])
    return out


a, b = events(sys.argv[1]), events(sys.argv[2])
print(f"A: {len(a)} events, B: {len(b)} events")
for line in difflib.unified_diff(a, b, "A", "B", n=1, lineterm=""):
    print(line)
