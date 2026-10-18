#!/usr/bin/env python3
"""Capture once, replay both renderers, and save PNGs and numeric differences."""
import argparse
import concurrent.futures
import json
import os
from pathlib import Path
import subprocess

from PIL import Image, ImageChops, ImageDraw, ImageEnhance
import numpy as np

GAMES = {
    "o3auto": "Crash Bandicoot (USA)",
    "sh": "Silent Hill (USA)",
    "r4": "R4 - Ridge Racer Type 4 (USA)",
    "ctr": "CTR - Crash Team Racing (USA)",
    "gt2": "Gran Turismo 2 (USA) (Arcade Mode) (Rev 1)",
}


def images(reference, pica, output, stem, reference_label="P.E.Op.S."):
    a, b = Image.open(reference).convert("RGB"), Image.open(pica).convert("RGB")
    assert a.size == b.size
    a.save(output / (stem + "-reference.png"))
    b.save(output / (stem + "-pica.png"))
    diff = ImageChops.difference(a, b)
    ImageEnhance.Brightness(diff).enhance(3).save(output / (stem + "-diff.png"))
    w, h = a.size
    side = Image.new("RGB", (w * 3, h + 20))
    for x, im, label in [(0, a, reference_label), (w, b, "PICA model"), (2*w, ImageEnhance.Brightness(diff).enhance(3), "absolute difference x3")]:
        side.paste(im, (x, 20))
        ImageDraw.Draw(side).text((x+4, 3), label, fill="white")
    side.save(output / (stem + "-comparison.png"))
    error = np.abs(np.asarray(a, dtype=np.int16) - np.asarray(b, dtype=np.int16))
    return {"mae": float(error.mean()), "different_percent": float((error.max(axis=2)>0).mean()*100),
            "over_24_percent": float((error.max(axis=2)>24).mean()*100)}


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--build", type=Path, default=Path("/tmp/correct-sim"))
    p.add_argument("--devtools", type=Path, default=Path("/home/tony/Development/PS1_3ds/devtools"))
    p.add_argument("--output", type=Path, required=True)
    p.add_argument("--states", nargs="+", default=list(GAMES))
    p.add_argument("--frames", nargs="+", type=int, default=[2, 30, 60])
    p.add_argument("--reuse-traces", action="store_true")
    p.add_argument("--pica", type=Path, help="alternate PICA replay binary for deterministic A/B")
    p.add_argument("--reference", choices=["peops", "neon"], default="peops")
    args = p.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    options = Path(__file__).with_name("options.ini")

    def run(state):
        directory = args.output / state
        directory.mkdir(exist_ok=True)
        trace = args.build / (state + ".gputrace")
        if not args.reuse_traces:
            cue = next((args.devtools / "roms/psx" / GAMES[state]).glob("*.cue"))
            qemu = args.devtools / "dl/qemu-full/build/qemu-arm"
            with (directory / "capture.log").open("w") as log:
                subprocess.run([qemu, args.build/"runner_capture", cue, str(max(args.frames)),
                                args.build/"states"/(state+".st0"), options],
                               cwd=args.build, env=dict(os.environ, GPU_TRACE=str(trace), GPU_VERIFY="1"),
                               stdout=log, stderr=log, check=True)
        for renderer in [args.reference, "pica"]:
            dest = directory / renderer
            dest.mkdir(exist_ok=True)
            with (directory / (renderer + ".log")).open("w") as log:
                binary=args.pica if renderer=="pica" and args.pica else args.build/("replay-"+renderer)
                subprocess.run([binary, trace, dest], stdout=log, stderr=log,
                               env=dict(os.environ,GPU_FRAMES=",".join(map(str,args.frames))),check=True)
        result = {}
        for frame in args.frames:
            stem = f"frame-{frame:04d}"
            result[str(frame)] = images(directory/args.reference/(stem+".ppm"), directory/"pica"/(stem+".ppm"), directory, stem, "gpu_neon C" if args.reference=="neon" else "P.E.Op.S.")
        print(state, json.dumps(result), flush=True)
        return state, result

    with concurrent.futures.ThreadPoolExecutor(max_workers=3) as workers:
        results = dict(workers.map(run, args.states))
    (args.output/"metrics.json").write_text(json.dumps(results, indent=2)+"\n")


if __name__ == "__main__":
    main()
