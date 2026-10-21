#!/usr/bin/env python3
"""Pin and verify the devkitPro toolchain a PSX3D build uses.

  toolchain.py packages            print "package" lock lines for what is installed
  toolchain.py abi                 print "abi" lock lines
  toolchain.py check              verify every lock line (CI, container build)
  toolchain.py check --abi         verify only what the private citro3d code depends on
                                   (the root Makefile runs this before every build)

tools/build/toolchain.lock lists the exact packages (name, version, hash of the
installed files) and the files that carry the citro3d private ABI. A package hash
covers the bytes on disk, not just the version string, so a rebuilt package with
the same version is caught as well. Deliberate SDK upgrades: update the lock in
the same commit, see docs/building.md.
"""
import argparse
import hashlib
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
LOCK = os.path.join(HERE, "toolchain.lock")
ABI_HEADER = os.path.join(ROOT, "psx3d/source/c3d_abi.h")
DKP = os.environ.get("DEVKITPRO", "/opt/devkitpro")


def sha256_file(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def tree_hash(entries):
    """entries: iterable of (relative path, absolute path)."""
    h = hashlib.sha256()
    for rel, path in sorted(entries):
        if os.path.islink(path):
            h.update(("L %s %s\n" % (rel, os.readlink(path))).encode())
        elif os.path.isfile(path):
            h.update(("F %s %d %s\n" % (rel, os.access(path, os.X_OK), sha256_file(path))).encode())
        else:
            h.update(("MISSING %s\n" % rel).encode())
    return h.hexdigest()


def pacman_dbs():
    for db in (os.path.join(DKP, "pacman/var/lib/pacman/local"), "/var/lib/pacman/local"):
        if os.path.isdir(db):
            yield db


def installed_packages():
    """name -> (version, tree hash) for every package that ships files under opt/devkitpro."""
    found = {}
    for db in pacman_dbs():
        for entry in sorted(os.listdir(db)):
            files = os.path.join(db, entry, "files")
            desc = os.path.join(db, entry, "desc")
            if not (os.path.isfile(files) and os.path.isfile(desc)):
                continue
            text = open(desc).read()
            name = re.search(r"%NAME%\n(.+)\n", text).group(1)
            version = re.search(r"%VERSION%\n(.+)\n", text).group(1)
            listing = open(files).read().partition("%FILES%\n")[2]   # empty for meta packages
            paths = [p for p in listing.split("\n") if p and not p.endswith("/") and not p.startswith("%")]
            if not any(p.startswith("opt/devkitpro/") for p in paths):
                continue
            found.setdefault(name, (version, tree_hash((p, "/" + p) for p in paths)))
    return found


def read_lock():
    lock = {"image": None, "package": {}, "abi": [], "tool": {}}
    for n, line in enumerate(open(LOCK), 1):
        line = line.split("#", 1)[0].strip()
        if not line:
            continue
        f = line.split()
        if f[0] == "image":
            lock["image"] = f[1]
        elif f[0] == "package":
            lock["package"][f[1]] = (f[2], f[3])
        elif f[0] == "abi":
            lock["abi"].append(f[1:])
        elif f[0] == "tool":
            lock["tool"][f[1]] = f[2:]
        else:
            sys.exit("%s:%d: unknown line %r" % (LOCK, n, line))
    return lock


def context_size_from_header():
    m = re.search(r"#define\s+C3D_ABI_CONTEXT_SIZE\s+(0x[0-9a-fA-F]+|\d+)", open(ABI_HEADER).read())
    return int(m.group(1), 0)


def library_symbol_size(lib, symbol):
    nm = os.path.join(os.environ.get("DEVKITARM", DKP + "/devkitARM"), "bin/arm-none-eabi-nm")
    out = subprocess.run([nm, "-S", "--defined-only", lib], capture_output=True, text=True, check=True).stdout
    for line in out.splitlines():
        f = line.split()
        if len(f) == 4 and f[3] == symbol:
            return int(f[1], 16)
    return None


# Everything the private citro3d code and the GX/GSP wraps depend on, relative to $DEVKITPRO.
ABI_FILES = (("file", "libctru/lib/libcitro3d.a"), ("file", "libctru/lib/libcitro2d.a"),
             ("file", "libctru/lib/libctru.a"), ("tree", "libctru/include"))


def abi_hash(kind, rel):
    path = os.path.join(DKP, rel)
    if kind == "file":
        return sha256_file(path) if os.path.isfile(path) else "missing"
    return tree_hash((os.path.relpath(os.path.join(d, f), path), os.path.join(d, f))
                     for d, _, fs in os.walk(path) for f in fs)


def check_abi(lock):
    bad = []
    for kind, rel, want in lock["abi"]:
        if (kind, rel) not in ABI_FILES:
            sys.exit("lock: unknown abi entry %s %s" % (kind, rel))
        got = abi_hash(kind, rel)
        if got != want:
            bad.append("  %s %s\n    locked  %s\n    present %s" % (kind, rel, want, got))
    lib = os.path.join(DKP, "libctru/lib/libcitro3d.a")
    expect, size = context_size_from_header(), library_symbol_size(lib, "__C3D_Context")
    if size != expect:
        bad.append("  sizeof(C3D_Context): psx3d/source/c3d_abi.h says %s, libcitro3d.a defines %s" % (expect, size))
    return bad


def check_packages(lock):
    have = installed_packages()
    bad = []
    for name, (version, want) in sorted(lock["package"].items()):
        if name not in have:
            bad.append("  %s %s: not installed" % (name, version))
        elif have[name][0] != version:
            bad.append("  %s: locked %s, installed %s" % (name, version, have[name][0]))
        elif have[name][1] != want:
            bad.append("  %s %s: same version but different files (rebuilt package?)" % (name, version))
    return bad


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("command", choices=("packages", "abi", "check"))
    ap.add_argument("--abi", action="store_true", help="check only the private citro3d ABI files")
    args = ap.parse_args()
    if args.command == "packages":
        for name, (version, digest) in sorted(installed_packages().items(), key=lambda kv: kv[0].lower()):
            print("package %s %s %s" % (name, version, digest))
        return
    if args.command == "abi":
        for kind, rel in ABI_FILES:
            print("abi %s %s %s" % (kind, rel, abi_hash(kind, rel)))
        return
    lock = read_lock()
    bad = check_abi(lock)
    what = "citro3d private ABI"
    if not args.abi:
        bad += check_packages(lock)
        what = "devkitPro toolchain"
    if bad:
        sys.stderr.write("\nThe %s differs from tools/build/toolchain.lock:\n%s\n\n"
                         "psx3d/source/renderqueue.c and gpu_cache.c use citro3d internals, so a different\n"
                         "SDK can compile and still misbehave. Build in the pinned container\n"
                         "(tools/build/container.sh), or after testing the new SDK on hardware update\n"
                         "the lock (docs/building.md, \"Upgrading the SDK\"). SDK_CHECK=0 skips this check.\n"
                         % (what, "\n".join(bad)))
        sys.exit(1)
    print("toolchain.py: %s matches toolchain.lock" % what)


if __name__ == "__main__":
    main()
