"""Writes a copy of the game's data folder with textures at half resolution.

For every NUT (NTP3) texture that has mipmaps, mip level 0 is dropped: level 1
(half width and height) becomes the base, so nothing is resampled. Textures
without mipmaps are left as they are. A changed file is written uncompressed
(the game reads plain "NUCC" files too); an unchanged one is copied as it is.

Texture chunk layout (big-endian), as found in the files:
  chunk header: u32 size, u32 map index, u16 version, u16 0
  chunk data:   u16 ?, u16 ?, u16 ?, u16 ?, u32 nut_size, then the NUT
  NUT: "NTP3", u16 0x0100, u16 count, 8 bytes; per texture at +0x10:
    +0x00 u32 total size, +0x08 u32 data size, +0x0C u16 header size,
    +0x11 u8 mip count, +0x13 u8 format, +0x14 u16 width, +0x16 u16 height,
    +0x30 u32 size of each mip level; the data follows the header.

usage: py -3 -I halve_textures.py <game folder> <output folder> <xctd_unpack.exe> <scratch file>
       [--min-size 64] [--only <subfolder of data>]
The output is the user's own game data: never distribute it.
"""
import argparse
import os
import shutil
import struct
import subprocess

p = argparse.ArgumentParser()
p.add_argument("game")
p.add_argument("out")
p.add_argument("exe")
p.add_argument("scratch")
# Halved stage textures make the battle's loading faster, and with it a race
# in the game shows up (a model drawn before one of its textures is
# registered: storm2/src/storm2_texture_lookup.cpp), whatever the minimum.
p.add_argument("--min-size", type=int, default=64,
               help="keep textures whose smaller side would drop below this")
p.add_argument("--only", default="", help="limit to data/<this> (others copied unchanged)")
p.add_argument("--unpack-only", action="store_true",
               help="write the files uncompressed without changing textures (test)")
p.add_argument("--keep-layout", action="store_true",
               help="test: pad each changed NUT to its old length so no chunk moves")
p.add_argument("--bisect-file", default="", help="test: halve only in this file name")
p.add_argument("--bisect-range", default="",
               help="test: only textures a:b[,c:d...] (index in the file)")
args = p.parse_args()


def halve_nut(nut):
    """Returns the NUT with mip 0 dropped from each texture, or None if unchanged."""
    if nut[:4] != b"NTP3":
        return None
    count = struct.unpack_from(">H", nut, 6)[0]
    out = bytearray(nut[:0x10])
    h = 0x10
    changed = False
    for _ in range(count):
        total, _, dsize, hsize = struct.unpack_from(">IIIH", nut, h)
        mips, w, hgt = nut[h + 0x11], *struct.unpack_from(">HH", nut, h + 0x14)
        header = bytearray(nut[h:h + hsize])
        data = nut[h + hsize:h + total]
        sizes = [struct.unpack_from(">I", nut, h + 0x30 + 4 * k)[0] for k in range(mips)]
        pow2 = w & (w - 1) == 0 and hgt & (hgt - 1) == 0
        # Non-power-of-two textures: the Xbox 360 sizes their mip levels from
        # power-of-two dimensions, so level 1 is not w/2 x h/2 (left alone).
        if (pow2 and mips > 1 and min(w, hgt) // 2 >= args.min_size and sum(sizes) == dsize
                and 0x30 + 4 * mips <= hsize):
            drop = sizes[0]
            data = data[drop:]
            # The mip size table is padded to 16 bytes, so the header can
            # shrink by 16 (e.g. 9 -> 8 levels: 0x80 -> 0x70); the extension
            # blocks after it (eXt, GIDX) keep their contents.
            align16 = lambda v: (v + 15) & ~15
            table_end = align16(0x30 + 4 * mips)
            new_table_end = align16(0x30 + 4 * (mips - 1))
            table = b"".join(struct.pack(">I", s) for s in sizes[1:])
            table += bytes(new_table_end - 0x30 - len(table))
            header = bytearray(header[:0x30]) + table + header[table_end:]
            new_hsize = len(header)
            struct.pack_into(">I", header, 0x00, new_hsize + len(data))
            struct.pack_into(">I", header, 0x08, dsize - drop)
            struct.pack_into(">H", header, 0x0C, new_hsize)
            header[0x11] = mips - 1
            struct.pack_into(">HH", header, 0x14, w // 2, hgt // 2)
            changed = True
        out += header + data
        h += total
    return bytes(out) if changed else None


texture_index = [0]


def halve_file(data):
    """Returns (new data, textures halved, bytes saved)."""
    texture_index[0] = 0
    out = bytearray()
    last = 0
    halved = saved = 0
    pos = 0
    while True:
        i = data.find(b"NTP3", pos)
        if i < 0:
            break
        pos = i + 4
        if i < 24:
            continue
        chunk_size = struct.unpack_from(">I", data, i - 24)[0]
        nut_size = struct.unpack_from(">I", data, i - 4)[0]
        if chunk_size != nut_size + 12:
            continue  # not a texture chunk laid out as expected
        nut = data[i:i + nut_size]
        index = texture_index[0]
        texture_index[0] += 1
        if args.bisect_range:
            ranges = [tuple(int(x) for x in r.split(":")) for r in args.bisect_range.split(",")]
            if not any(a <= index < b for a, b in ranges):
                continue
        new = halve_nut(nut)
        if new is None:
            continue
        if args.keep_layout:
            new += bytes(len(nut) - len(new))  # the NUT's texture count ends the parse
        out += data[last:i - 24]
        # Chunk data prefix: u16 0x64, u16 width, u16 height, u16 ?, u32 nut size.
        prefix = bytearray(data[i - 12:i])
        w, hgt = struct.unpack_from(">HH", new, 0x10 + 0x14)
        struct.pack_into(">HH", prefix, 2, w, hgt)
        struct.pack_into(">I", prefix, 8, len(new))
        out += struct.pack(">I", len(new) + 12) + data[i - 20:i - 12] + bytes(prefix)
        out += new
        last = i + nut_size
        pos = last
        halved += 1
        saved += nut_size - len(new)
    if not halved:
        return None, 0, 0
    out += data[last:]
    return bytes(out), halved, saved


files = halved_files = textures = 0
saved_total = 0
for dirpath, _, names in os.walk(args.game):
    rel_dir = os.path.relpath(dirpath, args.game)
    os.makedirs(os.path.join(args.out, rel_dir), exist_ok=True)
    for name in names:
        src = os.path.join(dirpath, name)
        dst = os.path.join(args.out, rel_dir, name)
        files += 1
        in_scope = not args.only or rel_dir.replace("\\", "/").startswith("data/" + args.only)
        if name.endswith(".xfbin") and in_scope:
            with open(src, "rb") as f:
                magic = f.read(4)
            data = None
            if magic == b"\x0f\xf5\x12\xed":
                if subprocess.run([args.exe, src, args.scratch], capture_output=True).returncode == 0:
                    data = open(args.scratch, "rb").read()
            elif magic == b"NUCC":
                data = open(src, "rb").read()
            bisect_other = args.bisect_file and name != args.bisect_file
            if data is not None and (args.unpack_only or bisect_other) and magic != b"NUCC":
                with open(dst, "wb") as f:
                    f.write(data)
                halved_files += 1
                continue
            if data is not None:
                new, n, saved = halve_file(data)
                if new is not None:
                    with open(dst, "wb") as f:
                        f.write(new)
                    halved_files += 1
                    textures += n
                    saved_total += saved
                    continue
        shutil.copyfile(src, dst)
print(f"{files} files, {halved_files} rewritten, {textures} textures halved, "
      f"{saved_total / 2**20:.0f} MiB of texture data saved")
