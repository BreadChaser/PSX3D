#!/usr/bin/env python3
"""PSX3D debug control client. No InputRedirection, FTP or NTR dependencies."""
from __future__ import annotations
import argparse
import json
import os
from pathlib import Path
import socket
import sys
import tempfile
import time

CONSOLES = {"old": "192.168.1.147", "new": "192.168.1.135"}
MAX_RESPONSE = 2 * 1024 * 1024
MAX_PUT = 8 * 1024 * 1024
TOKEN_FILE = Path(__file__).resolve().parent / "fastloop" / "token"

class ControlError(RuntimeError):
    pass

class Client:
    def __init__(self, console, token=None, port=5555, timeout=120):
        self.host = CONSOLES.get(console, console)
        if token is None:
            token = os.environ.get("PSX3D_TOKEN", "")
            if not token and TOKEN_FILE.exists():
                token = TOKEN_FILE.read_text().strip()
        self.token = token
        self.port, self.timeout = port, timeout

    def request(self, command, out=None, body=None):
        """Send one command. With out (a Path), a FILE reply streams to disk in
        chunks and the byte count is returned; body is raw data sent after the line."""
        if "\n" in command or "\r" in command or "\x00" in command:
            raise ControlError("Commands must be a single line (separate bench runs with ';')")
        line = ((self.token + " ") if self.token else "") + command
        if len(line.encode()) > 16382:
            raise ControlError("Command exceeds console's 16 KiB limit")
        with socket.create_connection((self.host, self.port), timeout=self.timeout) as sock:
            sock.settimeout(self.timeout)
            sock.sendall(line.encode() + b"\n" + (body or b""))
            with sock.makefile("rb") as reader:
                header = reader.readline(100)
                parts = header.split()
                if len(parts) != 2 or parts[0] not in (b"JSON", b"PNG", b"FILE"):
                    raise ControlError(f"Invalid response header: {header!r}")
                try:
                    size = int(parts[1])
                except ValueError as exc:
                    raise ControlError("Invalid response size") from exc
                if out is not None and parts[0] == b"FILE" and size >= 0:
                    return self._stream(reader, size, Path(out))
                if not 0 <= size <= MAX_RESPONSE:
                    raise ControlError(f"Response too large: {size}")
                data = reader.read(size)
                if len(data) != size:
                    raise ControlError(f"Truncated response ({len(data)}/{size})")
        if parts[0] == b"JSON":
            value = json.loads(data)
            if value.get("error") and value.get("ok") is not True:
                raise ControlError(value["error"])
            return value
        if out is not None:
            Path(out).write_bytes(data)
            return len(data)
        return data

    @staticmethod
    def _stream(reader, size, out):
        part = out.with_name(out.name + ".part")
        left = size
        with part.open("wb") as f:
            while left:
                chunk = reader.read(min(left, 1 << 16))
                if not chunk:
                    part.unlink()
                    raise ControlError(f"Truncated file ({size - left}/{size})")
                f.write(chunk)
                left -= len(chunk)
        part.replace(out)
        return size

    def get(self, remote, out):
        return self.request("get " + remote, out=out)

    def put(self, local, remote):
        data = Path(local).read_bytes()
        if len(data) > MAX_PUT:
            raise ControlError(f"put is limited to {MAX_PUT} bytes")
        return self.request(f"put {remote} {len(data)}", body=data)

    def put_game_file(self, local, remote, overwrite=False, progress=None):
        """Stream one disc file with put-game (dev builds): no size cap below 4 GiB."""
        local = Path(local)
        size = local.stat().st_size
        line = ((self.token + " ") if self.token else "") + f"put-game {remote} {size}" + (" overwrite" if overwrite else "")
        with socket.create_connection((self.host, self.port), timeout=self.timeout) as sock:
            sock.settimeout(self.timeout)
            sock.sendall(line.encode() + b"\n")
            sent, t0, last = 0, time.monotonic(), 0.0
            with local.open("rb") as f:
                while True:
                    chunk = f.read(1 << 16)
                    if not chunk:
                        break
                    try:
                        sock.sendall(chunk)
                    except OSError:
                        break  # the console refused or aborted: its JSON reply says why
                    sent += len(chunk)
                    now = time.monotonic()
                    if progress and (now - last >= 2 or sent == size):
                        last = now
                        progress(sent, size, sent / max(now - t0, 1e-6))
            with sock.makefile("rb") as reader:
                header = reader.readline(100).split()
                if len(header) != 2 or header[0] != b"JSON":
                    raise ControlError(f"Invalid response header: {header!r}")
                value = json.loads(reader.read(int(header[1])))
        if value.get("error"):
            raise ControlError(value["error"])
        value["seconds"] = round(time.monotonic() - t0, 1)
        return value

    def games_root(self, tmp):
        """The console's games_dir (config.ini), default sdmc:/roms/psx."""
        try:
            self.get("sdmc:/3ds/PSX3D/config.ini", tmp)
            for line in Path(tmp).read_text(errors="replace").splitlines():
                if line.startswith("games_dir=") and line[10:].strip():
                    return line[10:].strip().rstrip("/")
        except (OSError, ControlError):
            pass
        return "sdmc:/roms/psx"

    def get_state(self, title, slot, out, thumbnail=False):
        title = str(title).replace(" ", "_")  # the console reads '_' as a space
        return self.request(f"get state {title} {slot}" + (" thm" if thumbnail else ""), out=out)

HELP = """commands: status stats load set shot bench update quit exit ...
file API (library only; refused while a game or batch runs):
  ls sdmc:/path                  list a folder (name, dir, size, mtime)
  get sdmc:/path -o FILE         download any SD file (streamed)
  put LOCAL sdmc:/3ds/PSX3D/...  upload <= 8 MiB to states/cheats/saves/covers/recordings/games
                                 or switches.txt/config.ini/games.ini (atomic replace)
  put-game LOCAL_DIR [--overwrite]  dev builds: upload a game folder's disc files
                                 (.cue/.bin/.chd/.m3u/.pbp/.iso/.img, < 4 GiB each) to
                                 <games_dir>/<folder>/ via .part + rename, then rescan
  crashes                        list Luma dumps (sdmc:/luma/dumps/arm11, arm9)
  logs                           list log.txt (newest) .. log.4.txt
  states Game_Title              list save-state slots
  get-state Game_Title SLOT [-o FILE] [--thm]   download .stN (or .sta for 'auto') and its .thm
"""

def safe_name(title):
    return "".join(c if c.isalnum() or c in "-_." else "_" for c in title)

def main():
    parser = argparse.ArgumentParser(description=__doc__, epilog=HELP,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("console", help="old, new, or LAN IPv4")
    parser.add_argument("command", nargs="+", help="console command or file-API helper (see below)")
    parser.add_argument("--token", default=None)
    parser.add_argument("--port", type=int, default=5555)
    parser.add_argument("--timeout", type=float, default=120)
    parser.add_argument("-o", "--out", type=Path, help="write PNG or downloaded file here")
    parser.add_argument("--thm", action="store_true", help="get-state: also fetch the thumbnail")
    parser.add_argument("--overwrite", action="store_true", help="put-game: replace existing game files")
    args = parser.parse_args()
    client = Client(args.console, args.token, args.port, args.timeout)
    words = args.command
    try:
        if words[0] == "put":
            if len(words) != 3:
                raise ControlError("usage: put LOCAL sdmc:/3ds/PSX3D/...")
            print(json.dumps(client.put(words[1], words[2]), indent=2))
            return
        if words[0] == "put-game":
            if len(words) != 2 or not Path(words[1]).is_dir():
                raise ControlError("usage: put-game LOCAL_GAME_FOLDER [--overwrite]")
            folder = Path(words[1])
            exts = {".cue", ".bin", ".chd", ".m3u", ".pbp", ".iso", ".img"}
            files = [p for p in folder.iterdir() if p.is_file() and p.suffix.lower() in exts]
            # Data first, sheets last: the library never sees a .cue whose .bin is missing.
            files.sort(key=lambda p: (p.suffix.lower() in (".cue", ".m3u"), p.name))
            if not files:
                raise ControlError("no disc files in that folder")
            with tempfile.TemporaryDirectory() as tmp:
                root = client.games_root(Path(tmp) / "config.ini")
            total = sum(p.stat().st_size for p in files)
            print(f"{len(files)} files, {total / 1e6:.1f} MB -> {root}/{folder.name}/", flush=True)
            try:
                have = {e["name"]: e["size"] for e in client.request(f"ls {root}/{folder.name}")["entries"]}
            except ControlError:
                have = {}
            t0 = time.monotonic()
            for p in files:
                if p.name in have and not args.overwrite:
                    if have[p.name] == p.stat().st_size:
                        print(f"  skip {p.name}: already on the card with the same size", flush=True)
                        continue
                    raise ControlError(f"{p.name} exists with a different size; use --overwrite")
                def show(sent, size, rate, name=p.name):
                    print(f"  {name}: {sent / 1e6:8.1f}/{size / 1e6:.1f} MB  {rate / 1e6:.2f} MB/s", flush=True)
                r = client.put_game_file(p, f"{root}/{folder.name}/{p.name}", args.overwrite, show)
                print(f"  ok {r['path']} {r['size']} bytes in {r['seconds']} s", flush=True)
            dt = time.monotonic() - t0
            print(f"done: {total / 1e6:.1f} MB in {dt:.0f} s ({total / 1e6 / max(dt, 1e-6):.2f} MB/s)")
            try:
                client.request("ui-key select")
                print("library rescanned (SELECT)")
            except ControlError as exc:
                print(f"rescan not sent ({exc}); press SELECT in the library")
            return
        if words[0] == "get-state":
            if len(words) != 3:
                raise ControlError("usage: get-state Game_Title SLOT [-o FILE] [--thm]")
            title, slot = words[1], words[2]
            ext = "sta" if slot == "auto" else f"st{slot}"
            out = args.out or Path(f"{safe_name(title)}.{ext}")
            print(f"{out}: {client.get_state(title, slot, out)} bytes")
            if args.thm:
                thm = out.with_name(out.name + ".thm")
                print(f"{thm}: {client.get_state(title, slot, thm, True)} bytes")
            return
        if words[0] == "get" and len(words) >= 2 and words[1].startswith("sdmc:/"):
            remote = " ".join(words[1:])
            out = args.out or Path(remote.rsplit("/", 1)[-1] or "download.bin")
            print(f"{out}: {client.get(remote, out)} bytes")
            return
        result = client.request(" ".join(words))
        if isinstance(result, bytes):
            if args.out:
                args.out.write_bytes(result)
                print(f"{args.out}: {len(result)} bytes")
            else:
                sys.stdout.buffer.write(result)
        else:
            print(json.dumps(result, indent=2))
    except (OSError, ValueError, ControlError) as exc:
        parser.exit(1, f"STOP: {exc}\n")

if __name__ == "__main__":
    main()
