#!/usr/bin/env python3
"""Summarize two cachesim snapshots without rounding away per-primitive costs.

primitive-profile.py ELF FIRST.txt LAST.txt --units 300
The unit is an emulated vblank for game runs, a primitive for primitive-bench.
The cache model is instructions + 70*I misses + 70*read misses + 4*store misses.
Interlocks are reported separately; neither model predicts hardware layout.
"""
import argparse
import bisect
import collections
import json
import subprocess


def profile(elf, first, last, units):
    symbols = []
    result = subprocess.run(["arm-linux-gnueabihf-nm", "-n", "-S", "--defined-only", elf],
                            capture_output=True, text=True, check=True)
    for line in result.stdout.splitlines():
        p = line.split(None, 3)
        if len(p) == 4 and len(p[1]) == 8 and p[2] in "tTwWbBdD":
            symbols.append((int(p[0], 16), int(p[1], 16), p[3]))
    symbols.sort()
    addresses = [s[0] for s in symbols]

    def load(path):
        data = {}
        with open(path) as f:
            for line in f:
                pc, count, *values = line.split()
                data[(int(pc, 16), int(count))] = list(map(int, values[:5]))
        return data

    a, b = load(first), load(last)
    rows = collections.defaultdict(lambda: [0] * 5)
    for (pc, count), values in b.items():
        before = a.get((pc, count), [0] * 5)
        diff = [v - w for v, w in zip(values, before)]
        if diff[0] <= 0:
            continue
        i = bisect.bisect_right(addresses, pc) - 1
        name = symbols[i][2] if i >= 0 and pc < symbols[i][0] + symbols[i][1] else "[translated]"
        diff[0] *= count
        for k in range(5):
            rows[name][k] += diff[k]
    output = {}
    for name, values in rows.items():
        keys = ["instructions", "imiss", "rmiss", "wmiss", "interlocks"]
        row = dict(zip(keys, (v / units for v in values)))
        row["cycles"] = row["instructions"] + row["imiss"] * 70 + row["rmiss"] * 70 + row["wmiss"] * 4
        output[name] = row
    total = {k: sum(r[k] for r in output.values()) for k in next(iter(output.values()))}
    return {"units": units, "total": total, "functions": output}


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("elf")
    parser.add_argument("first")
    parser.add_argument("last")
    parser.add_argument("--units", type=int, required=True)
    args = parser.parse_args()
    print(json.dumps(profile(args.elf, args.first, args.last, args.units), indent=2))
