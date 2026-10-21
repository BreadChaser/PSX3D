#!/usr/bin/env python3
"""Hash every loaded section of an ELF32 file (and the 3DSX beside it).

tools/build/elf-identity.py psx3d/PSX3D.elf

Two builds of one commit made the same executable when the output is identical:
debug info, symbol names of local files and the build path are not part of it.
"""
import hashlib
import os
import struct
import sys

SHT_NOBITS, SHF_ALLOC = 8, 2


def sections(data):
    if data[:4] != b"\x7fELF" or data[4] != 1 or data[5] != 1:
        sys.exit("not a little-endian ELF32 file")
    shoff, = struct.unpack_from("<I", data, 0x20)
    shentsize, shnum, shstrndx = struct.unpack_from("<HHH", data, 0x2E)
    headers = [struct.unpack_from("<IIIIIIIIII", data, shoff + i * shentsize) for i in range(shnum)]
    stroff = headers[shstrndx][4]
    for name, kind, flags, addr, off, size, *_ in headers:
        end = data.index(b"\0", stroff + name)
        if flags & SHF_ALLOC:
            yield data[stroff + name:end].decode(), kind, addr, off, size


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    path = sys.argv[1]
    data = open(path, "rb").read()
    for name, kind, addr, off, size in sections(data):
        digest = "(zero-filled)" if kind == SHT_NOBITS else hashlib.sha256(data[off:off + size]).hexdigest()
        print("%-22s 0x%08x %8d  %s" % (name, addr, size, digest))
    three = os.path.splitext(path)[0] + ".3dsx"
    if os.path.exists(three):
        print("%-22s %s" % (os.path.basename(three), hashlib.sha256(open(three, "rb").read()).hexdigest()))


if __name__ == "__main__":
    main()
