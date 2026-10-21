#!/usr/bin/env python3
"""Unpack a registry image by digest into a directory, without a container daemon.

tools/build/rootfs.py devkitpro/devkitarm@sha256:<index digest> <empty dir>

Every blob (manifest, config, layers) is verified against its sha256 before use.
tools/build/container.sh runs the result with bubblewrap when Docker is not usable.
Only anonymous Docker Hub pulls are supported; nothing is executed from the image.
"""
import hashlib
import json
import os
import shutil
import sys
import tarfile
import urllib.request

REGISTRY = "https://registry-1.docker.io"
AUTH = "https://auth.docker.io/token?service=registry.docker.io&scope=repository:%s:pull"
ACCEPT = ", ".join((
    "application/vnd.oci.image.index.v1+json",
    "application/vnd.docker.distribution.manifest.list.v2+json",
    "application/vnd.oci.image.manifest.v1+json",
    "application/vnd.docker.distribution.manifest.v2+json",
))


def fetch(repo, token, kind, digest, dest=None):
    """Download one blob or manifest, verify its digest, return bytes (or write to dest)."""
    req = urllib.request.Request("%s/v2/%s/%s/%s" % (REGISTRY, repo, kind, digest),
                                 headers={"Authorization": "Bearer " + token, "Accept": ACCEPT})
    h = hashlib.sha256()
    out = open(dest, "wb") if dest else None
    data = []
    with urllib.request.urlopen(req, timeout=120) as r:
        while True:
            chunk = r.read(1 << 20)
            if not chunk:
                break
            h.update(chunk)
            (out.write if out else data.append)(chunk)
    if out:
        out.close()
    if "sha256:" + h.hexdigest() != digest:
        sys.exit("digest mismatch for %s: got sha256:%s" % (digest, h.hexdigest()))
    return None if dest else b"".join(data)


def unpack(layer_path, root):
    """Extract one layer, applying OCI whiteouts. Device nodes are skipped."""
    with tarfile.open(layer_path) as tf:
        members = []
        for m in tf:
            base = os.path.basename(m.name)
            parent = os.path.join(root, os.path.dirname(m.name))
            if base == ".wh..wh..opq":
                if os.path.isdir(parent):
                    for name in os.listdir(parent):
                        remove(os.path.join(parent, name))
            elif base.startswith(".wh."):
                remove(os.path.join(parent, base[4:]))
            elif m.isdev() or m.isfifo():
                continue
            else:
                members.append(m)
        for m in members:
            target = os.path.join(root, m.name)
            if os.path.lexists(target) and not os.path.isdir(target):
                os.unlink(target)
        tf.extractall(root, members=members, filter="tar")


def remove(path):
    if os.path.isdir(path) and not os.path.islink(path):
        shutil.rmtree(path)
    elif os.path.lexists(path):
        os.unlink(path)


def main():
    if len(sys.argv) != 3 or "@sha256:" not in sys.argv[1]:
        sys.exit(__doc__)
    repo, digest = sys.argv[1].split("@")
    root = sys.argv[2]
    if os.path.isdir(root) and os.listdir(root):
        sys.exit("%s is not empty" % root)
    os.makedirs(root, exist_ok=True)
    with urllib.request.urlopen(AUTH % repo, timeout=60) as r:
        token = json.load(r)["token"]
    doc = json.loads(fetch(repo, token, "manifests", digest))
    if "manifests" in doc:
        pick = [m for m in doc["manifests"]
                if m.get("platform", {}).get("os") == "linux" and m["platform"].get("architecture") == "amd64"]
        doc = json.loads(fetch(repo, token, "manifests", pick[0]["digest"]))
    scratch = root.rstrip("/") + ".layers"
    os.makedirs(scratch, exist_ok=True)
    for i, layer in enumerate(doc["layers"]):
        blob = os.path.join(scratch, "%d.tar.gz" % i)
        print("layer %d/%d %s" % (i + 1, len(doc["layers"]), layer["digest"]), flush=True)
        fetch(repo, token, "blobs", layer["digest"], blob)
        unpack(blob, root)
        os.unlink(blob)
    os.rmdir(scratch)
    cfg = json.loads(fetch(repo, token, "blobs", doc["config"]["digest"]))
    with open(os.path.join(root, ".image-config.json"), "w") as f:
        json.dump(cfg, f, indent=1)
    print("unpacked into", root)


if __name__ == "__main__":
    main()
