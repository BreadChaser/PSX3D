#!/usr/bin/env python3
"""Compare magenta pixels in renderer replays against a reference replay.

magenta_metrics.py REFERENCE_DIR CANDIDATE_DIR [CANDIDATE_DIR ...]
Each directory holds frame-NNNN.ppm from tools/gpu-compare replay binaries.
Magenta = R>150, B>130, G<100 (8-bit scanout). Per candidate it prints the
frames compared, magenta pixel totals, pixels magenta only in the candidate
("extra") or only in the reference ("missing"), and the largest per-frame
values. Exit status 1 if any candidate has extra magenta pixels.
"""
import sys
from pathlib import Path

import numpy as np
from PIL import Image


def load(p):
    return np.asarray(Image.open(p).convert("RGB")).astype(np.int16)


def magenta(a):
    return (a[..., 0] > 150) & (a[..., 2] > 130) & (a[..., 1] < 100)


def main():
    ref = Path(sys.argv[1])
    frames = sorted(ref.glob("frame-*.ppm"))
    failed = False
    for cand in map(Path, sys.argv[2:]):
        n = mag_ref = mag_cand = extra = missing = 0
        worst_extra = worst_diff = (0, "")
        mae = []
        for f in frames:
            c = cand / f.name
            if not c.exists():
                continue
            a, b = load(f), load(c)
            ma, mb = magenta(a), magenta(b)
            n += 1
            mag_ref += int(ma.sum()); mag_cand += int(mb.sum())
            e = int((mb & ~ma).sum()); extra += e
            missing += int((ma & ~mb).sum())
            d = int((np.abs(a - b).max(axis=2) > 24).sum())
            worst_extra = max(worst_extra, (e, f.stem))
            worst_diff = max(worst_diff, (d, f.stem))
            mae.append(float(np.abs(a - b).mean()))
        failed |= extra > 0
        print(f"{cand}: frames {n} magenta ref {mag_ref} cand {mag_cand} "
              f"extra {extra} missing {missing} max-extra {worst_extra} "
              f"max >24 pixels {worst_diff} mean MAE {np.mean(mae):.3f}")
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
