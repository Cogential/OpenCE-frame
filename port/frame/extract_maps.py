#!/usr/bin/env python3
"""Copies the maps folder out of an Xbox disc image, for run.sh's first start.

The desktop ports do this in the game (port/linux/src/xiso.c); the Frame build
runs the Android port's game, which leaves it to its launcher. This follows
xiso.c: XDVDFS with 2048-byte sectors, the volume descriptor at 0x10000 of the
game partition, and directories whose entries form a binary tree. The files go
to <destination>/maps.partial, renamed to maps once they are all there.

Usage: extract_maps.py <disc image> <destination>
"""
import os
import shutil
import struct
import sys

SECTOR = 2048
MAGIC = b"MICROSOFT*XBOX*MEDIA"
PARTITION_OFFSETS = (0, 0x0FD90000, 0x02080000, 0x18300000)
DIRECTORY = 0x10


def main(image_path, destination):
    image = open(image_path, "rb")
    for base in PARTITION_OFFSETS:
        image.seek(base + 0x10000)
        if image.read(len(MAGIC)) == MAGIC:
            break
    else:
        sys.exit(f"{image_path} is not an Xbox disc image")
    root_sector, root_size = struct.unpack("<II", image.read(8))

    def entries(sector, size):
        image.seek(base + sector * SECTOR)
        table = image.read(size)
        found = []
        pending = [0]
        while pending:
            offset = pending.pop() * 4
            if offset + 14 > len(table):
                continue
            left, right, start, length, attributes, name_length = struct.unpack_from("<HHIIBB", table, offset)
            if left == 0xFFFF:
                continue
            name = table[offset + 14:offset + 14 + name_length].decode("latin-1")
            found.append((name, start, length, attributes & DIRECTORY))
            pending += [subtree for subtree in (left, right) if subtree]
        return found

    def copy_tree(sector, size, folder):
        os.makedirs(folder, exist_ok=True)
        for name, start, length, is_directory in entries(sector, size):
            path = os.path.join(folder, name)
            if is_directory:
                copy_tree(start, length, path)
                continue
            image.seek(base + start * SECTOR)
            with open(path, "wb") as out:
                left = length
                while left:
                    chunk = image.read(min(left, 1 << 22))
                    if not chunk:
                        sys.exit(f"{image_path} ends inside {name}")
                    out.write(chunk)
                    left -= len(chunk)
            print(f"{length:>11} {name}", flush=True)

    maps = [e for e in entries(root_sector, root_size) if e[0].lower() == "maps" and e[3]]
    if not maps:
        sys.exit(f"{image_path} has no maps folder")
    partial = os.path.join(destination, "maps.partial")
    shutil.rmtree(partial, ignore_errors=True)
    copy_tree(maps[0][1], maps[0][2], partial)
    os.replace(partial, os.path.join(destination, "maps"))


if __name__ == "__main__":
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    main(sys.argv[1], sys.argv[2])
