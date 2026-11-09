"""Inventory of the NUT (NTP3) textures inside the game's .xfbin files.

Unpacks each compressed file with xctd_unpack to a scratch file, finds the
NTP3 blocks and sums the texture data by format and size: how much halving
the resolution (dropping mip level 0) would save.

usage: py -3 -I texture_inventory.py <game data folder> <xctd_unpack.exe> <scratch file>
"""
import collections
import os
import struct
import subprocess
import sys

root, exe, scratch = sys.argv[1], sys.argv[2], sys.argv[3]
FORMATS = {0: "DXT1", 1: "DXT3", 2: "DXT5", 6: "5551", 7: "4444", 8: "565", 14: "8888", 17: "8888b"}

by_kind = collections.Counter()
count_kind = collections.Counter()
mip0 = 0
total = 0
textures = 0
no_mips = 0
by_folder = collections.Counter()
for dirpath, _, files in os.walk(root):
    for name in files:
        if not name.endswith(".xfbin"):
            continue
        path = os.path.join(dirpath, name)
        with open(path, "rb") as f:
            magic = f.read(4)
        if magic == b"\x0f\xf5\x12\xed":
            if subprocess.run([exe, path, scratch], capture_output=True).returncode != 0:
                continue
            data = open(scratch, "rb").read()
        else:
            data = open(path, "rb").read()
        folder = os.path.relpath(dirpath, root).split(os.sep)[0]
        pos = 0
        while True:
            i = data.find(b"NTP3", pos)
            if i < 0:
                break
            pos = i + 4
            count = struct.unpack_from(">H", data, i + 6)[0]
            h = i + 0x10
            for _ in range(count):
                if h + 0x30 > len(data):
                    break
                tsize, _, dsize, hsize = struct.unpack_from(">IIIH", data, h)
                mips, fmt, w, hgt = data[h + 0x11], data[h + 0x13], *struct.unpack_from(">HH", data, h + 0x14)
                if hsize < 0x30 or tsize < dsize or dsize == 0:
                    break
                sizes = [struct.unpack_from(">I", data, h + 0x30 + 4 * k)[0] for k in range(min(mips, 16))] if mips > 1 else [dsize]
                kind = f"{FORMATS.get(fmt, fmt)} {w}x{hgt}"
                by_kind[kind] += dsize
                count_kind[kind] += 1
                total += dsize
                by_folder[folder] += dsize
                textures += 1
                if mips > 1:
                    mip0 += sizes[0]
                else:
                    no_mips += 1
                    mip0 += dsize * 3 // 4
                h += tsize
print(f"{textures} textures, {total / 2**20:.0f} MiB of texture data; dropping level 0 saves "
      f"~{mip0 / 2**20:.0f} MiB ({no_mips} without mipmaps would need resampling)")
print("by folder (MiB):", {k: round(v / 2**20) for k, v in by_folder.most_common()})
print("largest kinds:")
for kind, size in by_kind.most_common(25):
    print(f"  {kind:>16}: {count_kind[kind]:5} textures, {size / 2**20:7.1f} MiB")
