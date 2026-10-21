#!/usr/bin/env python3
"""Build a preset update pack and its manifest (docs/presets.md "Automatic updates").

    python3 tools/presets/pack.py --out DIR [--presets psx3d/romfs/presets] [--schema-min N]

Writes DIR/presets-d<D>-s<S>.json (the manifest the app fetches) and
DIR/presets-d<D>-s<S>-v<N>.pack, where D is defaults.ini's defaults_version,
S is PRESET_SCHEMA_VERSION from psx3d/source/presetupdate.h and N is the pack
VERSION. Runs tools/presets/check.py first. Publishes nothing.

Pack format (read by psx3d/source/presetupdate.c pu_unpack):
    PSX3D-PRESETS 1\\n
    <name> <size>\\n<size bytes>     one per file: VERSION and every *.ini but defaults.ini
    END\\n
"""
import argparse
import hashlib
import json
import pathlib
import re
import subprocess
import sys

REPO = pathlib.Path(__file__).resolve().parents[2]
PACK_MAX = 512 * 1024          # presetupdate.h PU_PACK_MAX
FILES_MAX = 256                # PU_FILES_MAX (.ini files + VERSION)
FILE_MAX = 512 * 255           # PU_FILE_MAX
NAME = re.compile(r"^[a-z0-9-]{1,59}\.ini$")


def fail(msg):
    sys.exit(f"pack.py: {msg}")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--out", required=True, type=pathlib.Path)
    ap.add_argument("--presets", type=pathlib.Path, default=REPO / "psx3d/romfs/presets")
    ap.add_argument("--schema-min", type=int, help="oldest app preset schema this pack still suits (default: current)")
    ap.add_argument("--no-check", action="store_true", help="skip check.py (tests only)")
    a = ap.parse_args()

    if not a.no_check:
        subprocess.run([sys.executable, str(REPO / "tools/presets/check.py")], check=True)
    header = (REPO / "psx3d/source/presetupdate.h").read_text()
    schema = int(re.search(r"^#define PRESET_SCHEMA_VERSION (\d+)$", header, re.M).group(1))
    schema_min = a.schema_min or schema
    if not 1 <= schema_min <= schema:
        fail(f"--schema-min must be 1..{schema}")
    defaults = (a.presets / "defaults.ini").read_text()
    d = re.search(r"^defaults_version\s*=\s*(\d+)\s*$", defaults, re.M)
    if not d:
        fail("defaults.ini has no defaults_version")
    dver = int(d.group(1))
    vtext = (a.presets / "VERSION").read_bytes()
    if not re.fullmatch(rb"[1-9][0-9]{0,5}\n", vtext):
        fail("VERSION must be one positive number and a newline")
    version = int(vtext)

    names = sorted(p.name for p in a.presets.glob("*.ini") if p.name != "defaults.ini")
    if len(names) + 1 > FILES_MAX:
        fail("too many files")
    out = bytearray(b"PSX3D-PRESETS 1\n")
    for name in ["VERSION"] + names:
        if name != "VERSION" and not NAME.match(name):
            fail(f"file name not allowed in a pack: {name}")
        data = (a.presets / name).read_bytes()
        if not 1 <= len(data) <= FILE_MAX:
            fail(f"{name}: size {len(data)} out of range")
        out += f"{name} {len(data)}\n".encode() + data
    out += b"END\n"
    if len(out) > PACK_MAX:
        fail(f"pack is {len(out)} bytes, limit {PACK_MAX}")

    a.out.mkdir(parents=True, exist_ok=True)
    stem = f"presets-d{dver}-s{schema}"
    pack_name = f"{stem}-v{version}.pack"
    (a.out / pack_name).write_bytes(out)
    manifest = {
        "format": 1,
        "pack_version": version,
        "schema_min": schema_min,
        "schema_max": schema,
        "defaults_version": dver,
        "file": pack_name,
        "size": len(out),
        "sha256": hashlib.sha256(out).hexdigest(),
    }
    (a.out / f"{stem}.json").write_text(json.dumps(manifest, indent=1) + "\n")
    print(f"{a.out / (stem + '.json')}: pack {version}, {len(names)} games, {len(out)} bytes, sha256 {manifest['sha256']}")


if __name__ == "__main__":
    main()
