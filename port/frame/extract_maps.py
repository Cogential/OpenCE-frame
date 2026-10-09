#!/usr/bin/env python3
"""Copies maps/ out of an Xbox XDVDFS disc image (the format handling of port/linux/src/xiso.c), for run.sh."""
import os, struct, sys
iso, dest = sys.argv[1], sys.argv[2]
f = open(iso, "rb")
MAGIC = b"MICROSOFT*XBOX*MEDIA"
for base in (0, 0x18300000, 0x0FD90000, 0x89D80000, 0x2080000):
    f.seek(base + 0x10000)
    if f.read(20) == MAGIC:
        break
else:
    sys.exit("not an Xbox disc image")
root_sector, root_size = struct.unpack("<II", f.read(8))

def walk(sector, size):
    f.seek(base + sector * 2048); data = f.read(size); out = []
    def node(off):
        if off * 4 + 14 > len(data): return
        l, r, s, sz, attr, nl = struct.unpack_from("<HHIIBB", data, off * 4)
        if l == 0xFFFF: return
        name = data[off * 4 + 14: off * 4 + 14 + nl].decode("latin-1")
        out.append((name, s, sz, attr & 0x10))
        if l: node(l)
        if r: node(r)
    node(0); return out

def copy(sector, size, path):
    f.seek(base + sector * 2048); left = size
    with open(path, "wb") as o:
        while left:
            b = f.read(min(left, 1 << 22)); o.write(b); left -= len(b)

maps = [e for e in walk(root_sector, root_size) if e[0].lower() == "maps" and e[3]]
if not maps: sys.exit("no maps folder")
os.makedirs(os.path.join(dest, "maps"), exist_ok=True)
def rec(sector, size, outdir):
    for name, s, sz, isdir in walk(sector, size):
        p = os.path.join(outdir, name)
        if isdir: os.makedirs(p, exist_ok=True); rec(s, sz, p)
        else: copy(s, sz, p); print(f"{sz:>11} {p}")
rec(maps[0][1], maps[0][2], os.path.join(dest, "maps"))
