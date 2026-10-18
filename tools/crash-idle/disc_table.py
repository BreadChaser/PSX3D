#!/usr/bin/env python3
"""Check that Crash's magenta colour table comes from the disc.

find:   disc_table.py find BIN RAMDUMP [--table 0x801de7a0]
        Searches the BIN's Mode 2 Form 1 user data for the table's first
        64 bytes (from a 2 MiB RAM dump), extends the first match in both
        directions and prints every offset plus SHA-256 of the matched span.
verify: disc_table.py verify BIN OFFSET LENGTH SHA256 [COUNT]
        Recomputes the hash at OFFSET and counts all copies, without a RAM dump.
No game data is stored in this script.
"""
import hashlib
import sys


def user_data(path):
    img = open(path, "rb").read()
    assert len(img) % 2352 == 0, "expected a raw 2352-byte-sector BIN"
    return b"".join(img[i + 24:i + 24 + 2048] for i in range(0, len(img), 2352))


def all_offsets(data, pat):
    out, k = [], data.find(pat)
    while k >= 0:
        out.append(k)
        k = data.find(pat, k + 1)
    return out


def main():
    if len(sys.argv) >= 4 and sys.argv[1] == "find":
        disc, ram = user_data(sys.argv[2]), open(sys.argv[3], "rb").read()
        table = int(sys.argv[5], 16) if len(sys.argv) > 5 else 0x801de7a0
        base = table & 0x1fffff
        k = disc.find(ram[base:base + 64])
        assert k >= 0, "table head not found on disc"
        before = after = 0
        while ram[base - before - 1] == disc[k - before - 1]:
            before += 1
        while ram[base + after] == disc[k + after]:
            after += 1
        span = ram[base - before:base + after]
        hits = all_offsets(disc, span)
        print("matched span: %d bytes before table, %d from table" % (before, after))
        print("span sha256", hashlib.sha256(span).hexdigest())
        print("copies", len(hits))
        print("offsets (user-data bytes)", " ".join(hex(h) for h in hits))
        # first word that differs after the span (a load-time relocated field)
        print("ram/disc after span: %s / %s" % (ram[base + after:base + after + 4].hex(),
                                               disc[k + after:k + after + 4].hex()))
    elif len(sys.argv) >= 6 and sys.argv[1] == "verify":
        disc = user_data(sys.argv[2])
        off, length, want = int(sys.argv[3], 0), int(sys.argv[4]), sys.argv[5]
        span = disc[off:off + length]
        ok = hashlib.sha256(span).hexdigest() == want
        n = len(all_offsets(disc, span))
        print("hash", "OK" if ok else "MISMATCH", "copies", n)
        if not ok or (len(sys.argv) > 6 and n != int(sys.argv[6])):
            sys.exit(1)
    else:
        print(__doc__)
        sys.exit(2)


if __name__ == "__main__":
    main()
