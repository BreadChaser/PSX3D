#!/usr/bin/env python3
"""600-frame PICA thread regression using the project's ignored host simulator.

Source devtools/env.sh first. Never connects to a console. A fresh simulator
copy is required because make does not track USE_ASYNC_GPU compiler flags.
"""
import argparse
from concurrent.futures import ThreadPoolExecutor
from collections import Counter
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess

repo = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--devtools", type=Path, default=Path.home() / "Development/PS1_3ds/devtools")
parser.add_argument("--sim", type=Path, default=Path("/tmp/n3thread-sim"))
parser.add_argument("--sdk", type=Path, help="current simulator fake3ds SDK")
parser.add_argument("--reuse", action="store_true", help="reuse a copy prepared by this script")
args = parser.parse_args()
sim, devtools = args.sim.resolve(), args.devtools.resolve()
if not args.reuse:
    # Copy only the runner, fake SDK, save states and PICA core; profiling
    # logs and the other renderer checkouts are unrelated to this test.
    sim.mkdir(parents=True)
    for name in ("core_pica", "fake3ds", "states"):
        shutil.copytree(args.sdk if name == "fake3ds" and args.sdk else devtools / "sim" / name, sim / name, symlinks=True,
            copy_function=shutil.copyfile, ignore=shutil.ignore_patterns(".git", "*.o", "*.d", "*.a"))
    for name in ("runner.c", "o3hw_off.ini"):
        shutil.copyfile(devtools / "sim" / name, sim / name)
    # Vendored core: copy all current sources, preserving only ctrsim's
    # host platform definitions from the simulator Makefile.libretro.
    old_make = (sim / "core_pica/Makefile.libretro").read_text()
    shutil.rmtree(sim / "core_pica")
    shutil.copytree(repo / "core", sim / "core_pica",
        ignore=shutil.ignore_patterns("*.o", "*.d", "*.a"))
    makefile = sim / "core_pica/Makefile.libretro"
    current = makefile.read_text()
    marker, end = "else ifeq ($(platform), ctrsim)", "else ifeq ($(platform), ctr)"
    offset = 0
    for occurrence in range(2):
        start = old_make.index(marker, offset)
        offset = old_make.index(end, start)
        pos = current.index(end, 0 if occurrence == 0 else current.index(end) + len(end))
        current = current[:pos] + old_make[start:offset] + current[pos:]
    makefile.write_text(current)
    # Match the SDK APIs/state constants added since the simulator snapshot.
    sdk = sim / "fake3ds/3ds.h"
    text = sdk.read_text()
    if "GX_TextureCopy" not in text:
        text += "\n#include <string.h>\nstatic inline void GX_TextureCopy(const void *src, unsigned in_dim, void *dst, unsigned out_dim, unsigned size, unsigned flags) { memcpy(dst, src, size); }\n"
    sdk.write_text(text)
    sdk = sim / "fake3ds/citro3d.h"
    text = sdk.read_text()
    for key, value in (("GPU_PREVIOUS", 109), ("GPU_TEXTURE1", 110), ("GPU_ADD_SIGNED", 111)):
        if key not in text: text += f"\n#define {key} {value}\n"
    # citro3d 1.7.1 dereferences textures on units 1/2 while checking type;
    # unlike unit 0, NULL is not a supported unbind on those units.
    text = text.replace("(void)u; (void)t; }", '(void)u; if (u > 0 && !t) { fprintf(stderr, "invalid NULL C3D_TexBind on unit %d\\n", u); abort(); } }')
    sdk.write_text(text)
    makefile = sim / "core_pica/Makefile.libretro"
    makefile.write_text(makefile.read_text().replace(
        "\tifneq ($(CTR_GPU),unai)\n\tUSE_ASYNC_GPU = 0\n\tendif\n", ""))
    # Check gpulib's queue invariants too, even in an otherwise release build.
    with (sim / "core_pica/Makefile").open("a") as out:
        out.write("\nplugins/gpulib/gpu_async.o: CFLAGS += -UNDEBUG\n")
    runner = sim / "runner.c"
    text = runner.read_text().replace("void pica_frame_started(void);",
        "void pica_thread_sync(void);\nvoid pica_frame_started(void);")
    text = text.replace("static int nopt;", "static int nopt;\nstatic bool opts_updated;")
    text = text.replace("*(bool *)data = false; return true;",
        "*(bool *)data = opts_updated; opts_updated = false; return true;")
    text = text.replace("\t\t{ void renderer_flush_queues", "\t\tpica_thread_sync();\n\t\t{ void renderer_flush_queues")
    # Exercise actual libretro option updates and both save/load directions.
    text = text.replace("\t\tretro_run();\n#ifdef GLES", """
        if (getenv("THREAD_STRESS") && i && i % 100 == 0) {
            for (int k = 0; k < nopt; k++)
                if (!strcmp(opt_k[k], "pcsx_rearmed_gpu_thread_rendering"))
                    snprintf(opt_v[k], 64, "%s", (i / 100) & 1 ? "disabled" : "enabled");
            opts_updated = true;
        }
        retro_run();
        if (getenv("THREAD_STRESS") && i % 100 == 50) {
            size_t size = retro_serialize_size();
            void *save = malloc(size);
            if (!save || !retro_serialize(save, size) || !retro_unserialize(save, size)) abort();
            free(save);
            fprintf(stderr, "thread stress save/restore frame %d\\n", i);
        }
#ifdef GLES""", 1)
    runner.write_text(text)
    core = sim / "core_pica"
    command = ["make", "-f", "Makefile.libretro", "platform=ctrsim", "CTR_GPU=pica", "USE_ASYNC_GPU=1"]
    with (sim / "thread-build.log").open("w") as log:
        subprocess.run(command + ["clean"], cwd=core, stdout=log, stderr=log, check=True)
        subprocess.run(command + ["-j16", "DEBUG_SYMS=1", "CFLAGS_OPT=-O2",
            f"PICA_CFLAGS=-I{sim / 'fake3ds'} -DPICA_SIM_HASH"],
            cwd=core, stdout=log, stderr=log, check=True)
        subprocess.run(["arm-linux-gnueabihf-gcc", "-O2", "-g", "-march=armv6k",
            "-mfloat-abi=hard", "-mfpu=vfp", "-marm", "-DPICA", "-I", "fake3ds",
            "-I", "core_pica/deps/libretro-common/include", "-o", "runner_pica",
            "runner.c", "core_pica/pcsx_rearmed_libretro_ctrsim.a", "-static", "-lpthread", "-lm"],
            cwd=sim, stdout=log, stderr=log, check=True)

qemu = devtools / "dl/qemu-full/build/qemu-arm"
roms = devtools / "roms/psx"
games = {"o3auto": "Crash Bandicoot (USA)", "sh": "Silent Hill (USA)",
         "r4": "R4 - Ridge Racer Type 4 (USA)",
         "ctr": "CTR - Crash Team Racing (USA)",
         "gt2": "Gran Turismo 2 (USA) (Arcade Mode) (Rev 1)"}
options = (sim / "o3hw_off.ini").read_text()
(sim / "thread_on.ini").write_text(options.replace("gpu_thread_rendering=disabled", "gpu_thread_rendering=enabled"))

def content_frames(path):
    # Additive batching may reorder only disjoint primitives. Worker drains and
    # state saves split batches differently, so the ordered checksum can differ.
    # Compare triangle topology, normalized state, positions and decoded samples
    # in EACH frame. A solid vertex's alpha=255 makes BL_HALF equivalent to
    # BL_OPAQUE; this is the renderer's documented solid/half shared state.
    state = None
    triangle = []
    counts = Counter()
    frames = []
    with path.open() as source:
        for line in source:
            if line.startswith("FRAME "):
                assert not triangle, path
                digest = hashlib.sha256()
                for item, count in sorted(counts.items()):
                    digest.update(repr((item, count)).encode())
                frame = int(line.split()[1])
                assert len(frames) <= frame < 600, (path, frame)
                while len(frames) < frame: frames.append(hashlib.sha256().hexdigest())
                frames.append(digest.hexdigest())
                counts.clear()
                continue
            words = tuple(int(word, 16) for word in line.split())
            if len(words) == 4:
                assert not triangle, path
                state = words
                continue
            assert len(words) == 8 and state is not None, path
            triangle.append(words)
            if len(triangle) == 3:
                normalized = state
                if state[1] == 1 and all(v[1] >> 24 == 255 for v in triangle):
                    normalized = (state[0], 0, *state[2:])
                counts[(normalized, tuple(triangle))] += 1
                triangle = []
    assert not triangle and not counts and len(frames) == 600, (path, len(frames))
    return frames


def run(case):
    state, mode = case
    cue = next((roms / games[state]).glob("*.cue"))
    ini = "o3hw_off.ini" if mode == "off" else "thread_on.ini"
    trace = sim / f"thread-{state}-{mode}.trace"
    env = dict(os.environ, SMALL="1", HASHEVERY="1", HTRACE=str(trace))
    if mode == "stress":
        env["THREAD_STRESS"] = "1"
    path = sim / f"thread-{state}-{mode}.log"
    with path.open("w") as log:
        subprocess.run([str(qemu), "./runner_pica", str(cue), "600", f"states/{state}.st0", ini],
            cwd=sim, env=env, stdout=log, stderr=log, check=True, timeout=180)
    text = path.read_text()
    hashed = re.search(r"draw hash (\w+)", text)
    assert hashed, path
    assert re.search(r"c3d draws/frame [1-9][0-9]*", text), path
    assert ("gpu thread: PICA enabled" in text) == (mode != "off"), path
    if mode == "stress":
        assert text.count("thread stress save/restore") == 6, path
        assert text.count("gpu thread: PICA enabled") >= 3, path
    content = content_frames(trace)
    trace.unlink()
    return state, mode, hashed[1], content

with ThreadPoolExecutor(max_workers=4) as pool:
    results = list(pool.map(run, [(s, m) for s in games for m in ("off", "on", "on2", "stress")]))
for state in games:
    hashes = [(m, h) for s, m, h, _ in results if s == state]
    print(state, hashes, flush=True)
    contents = [content for s, _, _, content in results if s == state]
    assert all(content == contents[0] for content in contents), state
    print(state, "600 per-frame triangle/state/texel checks match", flush=True)
(sim / "results.json").write_text(json.dumps({state: {
    mode: {"draw_hash": hashed, "frame_content_sha256": hashlib.sha256("".join(content).encode()).hexdigest()}
    for s, mode, hashed, content in results if s == state} for state in games}, indent=2) + "\n")
