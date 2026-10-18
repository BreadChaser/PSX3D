#!/usr/bin/env python3
"""600-frame PICA thread regression using the project's ignored host simulator.

Source devtools/env.sh first. Never connects to a console. A fresh simulator
copy is required because make does not track USE_ASYNC_GPU compiler flags.
"""
import argparse
from concurrent.futures import ThreadPoolExecutor
import os
from pathlib import Path
import re
import shutil
import subprocess

repo = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--devtools", type=Path, default=Path.home() / "Development/PS1_3ds/devtools")
parser.add_argument("--sim", type=Path, default=Path("/tmp/n3thread-sim"))
parser.add_argument("--reuse", action="store_true", help="reuse a copy prepared by this script")
args = parser.parse_args()
sim, devtools = args.sim.resolve(), args.devtools.resolve()
if not args.reuse:
    # Copy only the runner, fake SDK, save states and PICA core; profiling
    # logs and the other renderer checkouts are unrelated to this test.
    sim.mkdir(parents=True)
    for name in ("core_pica", "fake3ds", "states"):
        shutil.copytree(devtools / "sim" / name, sim / name, symlinks=True,
            copy_function=shutil.copyfile, ignore=shutil.ignore_patterns(".git", "*.o", "*.d", "*.a"))
    for name in ("runner.c", "o3hw_off.ini"):
        shutil.copyfile(devtools / "sim" / name, sim / name)
    # Preserve the simulator's ctrsim platform, but use the current renderer
    # and gpulib. The checked-in core makefile also handles dual builds.
    for name in ("Makefile", "libpcsxcore/misc.c", "plugins/gpulib/gpu.c",
                 "plugins/gpulib/gpu.h", "plugins/gpulib/gpu_async.c",
                 "plugins/gpulib/gpu_async.h", "plugins/gpulib/vout_pl.c"):
        shutil.copy2(repo / "core" / name, sim / "core_pica" / name)
    for source in (repo / "core/plugins/gpu_pica").glob("*.[ch]"):
        shutil.copy2(source, sim / "core_pica/plugins/gpu_pica" / source.name)
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
         "ctr": "CTR - Crash Team Racing (USA)",
         "gt2": "Gran Turismo 2 (USA) (Arcade Mode) (Rev 1)"}
options = (sim / "o3hw_off.ini").read_text()
(sim / "thread_on.ini").write_text(options.replace("gpu_thread_rendering=disabled", "gpu_thread_rendering=enabled"))

def run(case):
    state, mode = case
    cue = next((roms / games[state]).glob("*.cue"))
    ini = "o3hw_off.ini" if mode == "off" else "thread_on.ini"
    env = dict(os.environ)
    if mode == "stress":
        env["THREAD_STRESS"] = "1"
    path = sim / f"thread-{state}-{mode}.log"
    with path.open("w") as log:
        subprocess.run([str(qemu), "./runner_pica", str(cue), "600", f"states/{state}.st0", ini],
            cwd=sim, env=env, stdout=log, stderr=log, check=True, timeout=180)
    text = path.read_text()
    hashed = re.search(r"draw hash (\w+)", text)
    assert hashed, path
    assert ("gpu thread: PICA enabled" in text) == (mode != "off"), path
    if mode == "stress":
        assert text.count("thread stress save/restore") == 6, path
        assert text.count("gpu thread: PICA enabled") >= 3, path
    return state, mode, hashed[1]

with ThreadPoolExecutor(max_workers=4) as pool:
    results = list(pool.map(run, [(s, m) for s in games for m in ("off", "on", "on2", "stress")]))
for state in games:
    hashes = [(m, h) for s, m, h in results if s == state]
    print(state, hashes, flush=True)
    assert len({h for _, h in hashes}) == 1, state
