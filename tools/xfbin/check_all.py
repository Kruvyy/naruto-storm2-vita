"""Unpacks every compressed .xfbin of a game folder to a scratch file and
counts the ones that fail; prints the total uncompressed size.

usage: py -3 -I check_all.py <game data folder> <xctd_unpack.exe> <scratch file>
"""
import os
import subprocess
import sys

root, exe, scratch = sys.argv[1], sys.argv[2], sys.argv[3]
ok = failed = 0
packed = unpacked = 0
failures = []
for dirpath, _, files in os.walk(root):
    for name in files:
        if not name.endswith(".xfbin"):
            continue
        path = os.path.join(dirpath, name)
        with open(path, "rb") as f:
            if f.read(4) != b"\x0f\xf5\x12\xed":
                continue
        r = subprocess.run([exe, path, scratch], capture_output=True, text=True)
        if r.returncode == 0:
            ok += 1
            packed += os.path.getsize(path)
            unpacked += os.path.getsize(scratch)
        else:
            failed += 1
            failures.append(f"{os.path.relpath(path, root)}: {r.stderr.strip()}")
print(f"ok {ok}, failed {failed}; {packed / 2**20:.0f} MiB -> {unpacked / 2**20:.0f} MiB")
for line in failures[:20]:
    print(line)
