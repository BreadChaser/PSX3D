#!/usr/bin/env python3
"""Run bench.txt on one or both already-running PSX3D debug servers."""
from __future__ import annotations
import argparse
from concurrent.futures import ThreadPoolExecutor, as_completed
from contextlib import contextmanager
import csv
from functools import partial
import hashlib
import http.server
import io
import json
import os
from pathlib import Path
import signal
import tempfile
import threading
import time
import uuid
from psx3dctl import Client, ControlError

LAN_HOST, HTTP_PORT = "192.168.1.225", 8765

@contextmanager
def serve_cia(cia, bind=LAN_HOST, port=HTTP_PORT):
    # Serve only the copied CIA, never the project, devtools or a game folder.
    with tempfile.TemporaryDirectory(prefix="psx3d-cia-") as directory:
        path = Path(directory) / "PSX3D.cia"
        path.write_bytes(cia.read_bytes())
        digest = hashlib.sha256(path.read_bytes()).hexdigest()
        handler = partial(http.server.SimpleHTTPRequestHandler, directory=directory)
        server = http.server.ThreadingHTTPServer((bind, port), handler)
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        try:
            yield f"http://{bind}:{server.server_port}/PSX3D.cia size={path.stat().st_size} sha256={digest}"
        finally:
            server.shutdown()
            server.server_close()
            thread.join()

@contextmanager
def pause_viewers(enabled=True):
    # Pause only known host viewer request loops, not arbitrary Python workers.
    # Do not send NTR packets: active console-side NTR must be disabled manually.
    paused = []
    if enabled:
        for entry in Path("/proc").iterdir():
            try:
                if not entry.name.isdigit() or entry.stat().st_uid != os.getuid():
                    continue
                argv = (entry / "cmdline").read_bytes().split(b"\0")
                names = [Path(os.fsdecode(x)).name for x in argv[:3] if x]
                if not {"ntr.py", "ntr_o3ds.py", "ntr_both.py"}.intersection(names):
                    continue
                state = (entry / "stat").read_text().split()
                if state[2] in ("T", "t"):
                    continue
                pid, born = int(entry.name), state[21]
                os.kill(pid, signal.SIGSTOP)
                paused.append((pid, born))
            except (OSError, IndexError):
                continue
    try:
        yield
    finally:
        for pid, born in paused:
            try:
                if Path(f"/proc/{pid}/stat").read_text().split()[21] == born:
                    os.kill(pid, signal.SIGCONT)
            except OSError:
                pass

def bench_text(path, console):
    runs = []
    for line in path.read_text().splitlines():
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        line = line.replace("{state}", "2" if console == "old" else "0")
        if any(c in line for c in (";", "\t", "\r", "\x00")):
            raise ControlError("Bench runs cannot contain semicolons, tabs or NUL")
        if not line.startswith("game="):
            raise ControlError("Each bench line must begin game= (underscores for spaces)")
        runs.append(line)
    if not 1 <= len(runs) <= 32:
        raise ControlError("Bench requires 1..32 runs")
    if len(("bench " + ";".join(runs)).encode()) > 16200:
        raise ControlError("Combined batch exceeds 16 KiB; split the file")
    return runs

def wait_relaunch(client, previous, timeout):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        time.sleep(1)
        try:
            status = client.request("status")
            if status.get("session") != previous and status.get("mode") == "library":
                return
        except (OSError, ControlError):
            pass
    raise ControlError("Update did not relaunch in time; inspect console manually")

def run_console(console, args, update):
    started = time.monotonic()
    client = Client(console, args.token, args.port, args.timeout)
    status = client.request("status")
    if status["bench"]["active"]:
        raise ControlError(f"{console}: another batch is active")
    if update:
        print(f"[{console}] updating", flush=True)
        client.request("update " + update)
        wait_relaunch(client, status["session"], args.timeout)
    client.request("quit")
    runs = bench_text(args.bench, console)
    batch_id = f"{args.batch_id}-{console}"
    client.request("batch-id " + batch_id)
    client.request("bench " + ";".join(runs))
    print(f"[{console}] batch {batch_id}: {len(runs)} runs", flush=True)
    deadline = time.monotonic() + args.timeout + sum(
        int(next((w[8:] for w in r.split() if w.startswith("seconds=")), "20")) +
        int(next((w[7:] for w in r.split() if w.startswith("warmup=")), "5")) + 20
        for r in runs)
    last_run = None
    while time.monotonic() < deadline:
        time.sleep(1)
        status = client.request("status")
        batch = status["bench"]
        if batch["id"] != batch_id:
            raise ControlError(f"{console}: batch was replaced by another tester")
        if batch["run"] != last_run:
            print(f"[{console}] run {batch['run']+1}/{batch['runs']}: {status['game']}", flush=True)
            last_run = batch["run"]
        if not batch["active"]:
            break
    else:
        client.request("quit")
        raise ControlError(f"{console}: timed out; batch cancelled")
    out = args.out / console
    out.mkdir(parents=True, exist_ok=True)
    for name in ("results.tsv", "results.md"):
        data = client.request("get " + name)
        if not isinstance(data, bytes):
            raise ControlError("Expected result file")
        (out / name).write_bytes(data)
    rows = list(csv.DictReader(io.StringIO((out / "results.tsv").read_text()), delimiter="\t"))
    if not rows or any(row["batch"] != batch_id for row in rows):
        raise ControlError(f"{console}: stale or empty results")
    for row in rows:
        requested = row["request"].split()
        if "shot=0" not in requested and row["status"] == "ok":
            name = f"{batch_id}-{int(row['run']):02d}.png"
            # Compression happens after sampling on the low priority worker.
            for attempt in range(5):
                try:
                    (out / name).write_bytes(client.request("get " + name))
                    break
                except ControlError:
                    if attempt == 4:
                        raise
                    time.sleep(1)
    (out / "status.json").write_text(json.dumps(status, indent=2) + "\n")
    print(f"[{console}] completed in {time.monotonic()-started:.1f}s", flush=True)
    if len(rows) != len(runs) or any(r["status"] != "ok" for r in rows):
        raise ControlError(f"{console}: batch stopped early; partial results saved in {out}")
    return console, rows

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("bench", type=Path)
    parser.add_argument("consoles", nargs="?", choices=("old", "new", "both"), default="both")
    parser.add_argument("--cia", type=Path, help="self-update both consoles before running")
    parser.add_argument("--token", default=None)
    parser.add_argument("--port", type=int, default=5555)
    parser.add_argument("--timeout", type=float, default=180)
    parser.add_argument("--out", type=Path, default=Path(__file__).resolve().parent / "fastbench" / time.strftime("%Y%m%d_%H%M%S"))
    parser.add_argument("--keep-viewers", action="store_true", help="leave host NTR request loops running")
    args = parser.parse_args()
    args.batch_id = uuid.uuid4().hex[:12]
    names = ("old", "new") if args.consoles == "both" else (args.consoles,)
    # Validate all local inputs before opening any console connection.
    for name in names:
        bench_text(args.bench, name)
    args.out.mkdir(parents=True, exist_ok=True)
    from contextlib import nullcontext
    server = serve_cia(args.cia) if args.cia else nullcontext(None)
    errors, results = [], []
    with pause_viewers(not args.keep_viewers), server as update:
        with ThreadPoolExecutor(max_workers=len(names)) as pool:
            jobs = {pool.submit(run_console, name, args, update): name for name in names}
            for job in as_completed(jobs):
                try:
                    console, rows = job.result()
                    results.extend((console, row) for row in rows)
                except Exception as exc:
                    errors.append(f"{jobs[job]}: {exc}")
    print("\nConsole Run Game                          GPU FPS    Speed% Emu_ms Mcycles Black Status")
    for console, row in sorted(results, key=lambda x: (x[0], int(x[1]["run"]))):
        settings = dict(w.split("=", 1) for w in row["request"].split() if "=" in w)
        label = settings.get("game", "?").replace("_", " ")
        renderer = settings.get("renderer", "def")
        print(f"{console:7} {row['run']:>3} {label:29.29} {renderer:3} {row['fps']:>6.6} {row['speed_pct']:>6.6} "
              f"{row['emu_ms']:>6.6} {row['mcycles']:>7.7} {row['black_samples']:>5} {row['status']}")
    print(f"Artifacts: {args.out}")
    if errors:
        parser.exit(1, "\n".join("STOP: " + e for e in errors) + "\n")

if __name__ == "__main__":
    main()
