# Fast developer test loop

This is an opt-in developer facility, built into both the 3DSX and CIA. With no
`remote token=...` or `bench` in `sdmc:/3ds/PSX3D/switches.txt`, it starts no
control worker, sockets, history allocation, screenshot jobs, or extra probes.
The recorder adds an opt-in pause-menu action; see [recording and replay](recorder.md). A `3dslink` argument cannot enable the server.

## One-time setup

1. Install this CIA with the usual FBI workflow once. Ensure the game library,
   BIOS and benchmark states are already on SD. Old 3DS states normally use slot
   **2**, New 3DS slot **0**. Neither slot is written by a benchmark.
2. Put `remote token=choose-a-private-token` in
   `sdmc:/3ds/PSX3D/switches.txt`. Both `remote` and a nonempty token are required; the token has
   at most 95 bytes and contains no whitespace. For the host tools, export
   `PSX3D_TOKEN=choose-a-private-token`. These are plain LAN credentials, not TLS.
3. Start PSX3D once, from HOME for CIA updates. Leave it in the library. Both
   consoles must be on the same LAN as the host. No ftpd, NTR, screenshots of
   menus, InputRedirection, or repeated HOME presses are needed afterwards.
4. For measurements without NTR Wi-Fi traffic, start a session **without
   BootNTR/NTR-HR running** (reboot if necessary). Pausing a PC viewer alone does
   not stop an already-active console-side NTR stream.

Delete `remote` and restart PSX3D to turn the server off. Cover loading runs automatically in the library, including remote sessions.
A remote load, batch or update pauses the workers and closes their connections
first. They resume when the library becomes idle; gameplay never downloads covers.
The curl loop uses zero-timeout SOC polls and sleeps outside the socket service.
The ordinary library, controller inputs and game menu still work. Batch runs
ignore gameplay/touch inputs, fast-forward and rewind. Physical HOME/sleep can
still interrupt a run; do not use them during measurements or installation.

## Host tools

Tracked host clients are `tools/input-replay/psx3dctl.py` and
`tools/input-replay/fastbench.py`. Python 3's standard library is sufficient.
Choose the console addresses in those clients for your own LAN.

```sh
cd tools/input-replay
export PSX3D_TOKEN=choose-a-private-token
./psx3dctl.py old status
./psx3dctl.py new load Crash_Bandicoot state=0 renderer=gpu frameskip=off pmu=0x11,0x00
./psx3dctl.py new stats
./psx3dctl.py new set frameskip=auto
./psx3dctl.py new shot both -o screens.png
./psx3dctl.py new loadstate 0
./psx3dctl.py new savestate 9     # explicit write; can overwrite slot 9
./psx3dctl.py new quit            # unload, return to library, keep server
./psx3dctl.py new exit            # unload, normal shutdown to HOME
./fastbench.py six-runs.txt both
./fastbench.py six-runs.txt both --cia /path/to/PSX3D.cia
```

`old` = `192.168.1.147`, `new` = `192.168.1.135`; a numeric address can also be
used by `psx3dctl.py`. Default control port **5555**, overridable on the client
for local tests. `--token`, `--timeout`, `--out` and `PSX3D_TOKEN` are supported.
The driver gives each console a unique batch ID, rejects stale results, polls
status every second, runs both consoles in parallel and downloads TSV, Markdown
and per-run screenshots after completion. It stops on failed batches and saves
partial results. It does not automatically retry a mutation or an uncertain
install. The driver pauses known local `ntr*.py` viewer/request processes by PID
and resumes only those it paused; use `--keep-viewers` to opt out. It sends no
NTR or InputRedirection packets. A SIGKILL cannot run cleanup; resume any stopped
viewer manually with `kill -CONT <pid>` if the driver is forcibly killed.

The optional update server binds **192.168.1.225:8765**, serves only a temporary
copy named `PSX3D.cia`, and closes on normal completion or error. It includes
Content-Length, expected byte size and SHA256 in each update request. The driver
waits for a changed PSX3D session ID before starting the batch. Use `remote`
without startup `bench` for this workflow, so a relaunch does not auto-start an
old on-card batch. There is no FTP fallback or blind menu automation.

## UI screenshots without InputRedirection

These commands use the same opt-in, token-gated mailbox as every other remote
command. They open the real UI on the main thread; they do not synthesize images
or inject controller inputs. An active benchmark rejects UI commands.

```sh
./psx3dctl.py old ui library select=Crash_Bandicoot
./psx3dctl.py old shot both -o library.png
./psx3dctl.py old load Crash_Bandicoot state=2 renderer=gpu
./psx3dctl.py old ui pause
./psx3dctl.py old ui settings   # in-game settings, or global settings in library
./psx3dctl.py old savestate 9
./psx3dctl.py old ui states slot=9  # load browser only; does not load/write a slot
./psx3dctl.py old shot both -o states.png
./psx3dctl.py old ui resume
```

`ui library [select=<exact title>]` returns to the library.
Use underscores for title spaces. `ui pause`, `ui states [slot=0..9]` and
`ui resume` require a loaded game. `ui settings` opens the current context's
settings list. Commands acknowledge the request before the next UI frame is
drawn; allow that frame to complete before taking a screenshot. Modal settings
and the state browser keep servicing remote commands, so `load`, `quit` and
`update` can safely unwind them. `shot` defaults to both screens; `shot top`
keeps the existing top-only format. Benchmark PNGs remain top-only.

### Settings navigation captures

`ui-key a|b|x|y|up|down|left|right|l|r` navigates the real settings UI.
It uses the same token-gated, opt-in remote mailbox, is rejected during batches
or outside settings, and never injects gameplay input. Modal reset confirmations
continue servicing the mailbox; capture before confirming or cancel with `b`.
`ui-key debug-on` exposes the diagnostic settings page for capture only;
`debug-off` hides it. This inspection flag is volatile, never saved, and cleared
when settings closes. It does not enable core debug output.

The token-gated `covers` command reports worker, transfer, retry and cache counts,
last curl/HTTP result, and last/maximum stop latency in milliseconds. Cache counts
inspect only expected cover `.bin` files (valid files are exactly 65,536 bytes),
without arbitrary SD access. `covers refresh=<exact_title_or_library_index>`
queues a fresh HTTPS fetch in the idle library, bypassing its cached image for
that attempt. It preserves the on-disk cache unless a replacement decodes
successfully. It is a developer diagnostic, not a user setting.

## Batch format and output

Each non-comment line is one run; use underscores for spaces in library titles.
An exact case-insensitive title wins, otherwise a substring must match exactly
one library game. Absolute `sdmc:/...` paths also work; encode path spaces as
`%20` and leave literal underscores intact.

```text
# {state} is expanded by fastbench.py only: Old=2, New=0.
game=Crash_Bandicoot state={state} renderer=gpu frameskip=off warmup=5 seconds=20 pmu=0x11,0x00 shot=1
game=Crash_Team_Racing state={state} renderer=gpu frameskip=off warmup=5 seconds=20 pmu=0x11,0x00
game=Gran_Turismo_2 state={state} renderer=gpu frameskip=off warmup=5 seconds=20 pmu=0x11,0x00
```

Defaults: `warmup=5`, `seconds=20`, `shot=1`. Bounds: warmup 0..3600, seconds
1..3600, state 0..9, max 32 runs, 2047 bytes per run, combined wire request
under 16 KiB (including token). The first interval overlapping warm-up is
fully discarded; sampling begins at the next complete interval. At least one
extra second can therefore occur per run. Averages are weighted by each
interval's measured wall time, including slow intervals. Runs stop at the first
missing/ambiguous game, invalid switch, state failure or interruption.

Every run resets test overrides and goes through the complete normal
`retro_unload_game` / `retro_deinit` / `video_game_end` then renderer-init path.
No previous run's `o:`/clock/PMU/renderer override leaks into the next. User
per-game configs and curated defaults still apply unless overridden. Diagnostic
settings do not save to config; memory-card writes still use the normal game
save path. Resume prompts and automatic save-state writes are disabled while
this developer facility is enabled. Explicit `savestate` is available outside
an active batch and writes only the requested slot.

Supported run switches include `renderer=gpu` (legacy `renderer=sw` is accepted and ignored), `frameskip=off|auto|on`, `state=N`,
`pmu=AA,BB` (hex events), `clock=N`, `cdra=N`, up to eight `o:key=value`, `stats`,
`prof`, `noskip`, `fs1`, `fs3`, `gputhread`/`nogputhread`, `nosputhread`, `acap`,
`gldbg=N`, `vramdump=N`, `slider=N` and GPU submission switches.
`frameskip=off` consumes an `o:` slot. `stats` and `prof` retain their existing SD
logging/profiling cost; omit them when using the live metrics alone. Subsystem
startup switches (`nonet`, `nosound`, `csnd`, `noirrst`, `noaa`, `nolcd`, `nocovers`,
`o3ds`, `minimal`) belong in switches.txt, rather than per-run commands. Unknown
switches fail explicitly. `rtt=auto|old|new` selects the RTT implementation for each clean load.
The default `auto` starts with the original coarse tracker and single-square
cache. A GPU-dirty texture/CLUT read, VRAM readback or source copy activates
fine tracking and retained regions for the rest of that state/game session.
The transition drains queued readers and conservatively seeds generations/debt.
At the next texture lookup it drains readers again and retires the old enlarged
cache entries, so they cannot keep the retained-region allocator thrashing. The
decode that triggered activation finishes with its current allocation intact.
`rtt=old` forces the original path; `rtt=new` forces fine tracking from startup.
`set rtt=auto|old|new` repeats the last load with its original state/settings, then replies
after the new game starts; it does not change cache mappings during a live game.
`rttcache=old|new` selects round-one or retained-region caching after activation on a clean
`load`/batch; the default is `new`. Status includes build identity, `rttcache`, requested `rtt` policy, `free_heap` and `free_linear` (bytes) under `settings`.

`sdmc:/3ds/PSX3D/bench/results.tsv` contains the exact request, status, number of
samples, near-black samples, FPS, emulated game speed %, emulation/present/wait/
GPU/xfer/heap-flush/queue-wait/gap ms, main-thread M cycles/frame and two PMU
counter averages (in thousands), and PICA batch/primitive/texture/eviction/
invalidation/upload/sync/readback counters. PICA activity counters are rates per
second. `NA` means unavailable (e.g. PMU service failure).
Live JSON uses `null`. Raw audio diagnostics, all PICA text counters and the
picture probe are retained in the live history. `results.md` summarizes each run.
Screenshots are `bench/<batch-id>-<run-number>.png` (400x240 top left eye).

The existing `pic ... lum` probe samples brightness **once per second**. A
near-black sample means PICA luminance <=1/31 or top framebuffer <=2/255. The
report deliberately calls this `black_samples`; it is not a count of all black
frames and cannot prove an absence of flicker. PMU measures the emulation thread,
not the GPU worker; the existing GPU timing is not total GPU time for games
with multiple submissions/readbacks. Final screenshots are copied only once,
after sampling, and compressed on the worker. Requesting `shot` during a run
can cause a copy/synchronization hitch; use end-of-run shots for comparisons.

For an on-card batch, add `bench` to switches.txt and put the same format (with
numeric states, not `{state}`) in `sdmc:/3ds/PSX3D/bench.txt`. It auto-starts on
launch and returns to the library. Add `bench-exit=home` to shut down afterwards.
`bench.txt` is read only when the file contains `bench`. `bench file` can repeat
it in a remote session with that opt-in. A remote `bench game=...;game=...` sends
the list directly and needs no bench.txt. Delete the `bench` switch to prevent
repeating the on-card batch on future starts. Pull files without ftpd:

```sh
./psx3dctl.py old get results.tsv -o results.tsv
./psx3dctl.py old get results.md -o results.md
./psx3dctl.py old get manual-01.png -o run.png
```

## Protocol and thread ownership

TCP 5555, one client/command at a time. Send UTF-8 text ending in LF:
`[token ]command arguments\n`. Responses start with `JSON <length>\n`,
`PNG <length>\n` or `FILE <length>\n`, followed by exactly that many bytes; the
connection then closes. JSON errors use `ok:false`/`error`. `shot top`
is 400x240; `shot both` is a 400x480 PNG with the 320-wide bottom screen at the
lower left and black padding. `stats` returns a chronological `history` array of
at most 60 intervals; the final element is the latest. Each sample has monotonic
`seq` and `ms`; history resets on each successful game load.

The worker uses nonblocking sockets with bounded read/write idle timeouts. It
runs below the main thread's priority, on the syscore if time is granted, with a
core-0 fallback. It copies requests to a one-slot mailbox; the main thread polls
once per emulated frame and in the library/browser/game menu, and applies all
core/config/state commands at those boundaries. Network clients cannot call the
core or renderer directly. Slow/disconnected receivers never wait on emulation.
The main thread never waits on socket I/O. Loading, explicit state operations,
screenshot capture and deliberate unloading necessarily pause emulation; PNG
encoding and CIA downloads/install run on the worker. Replies to `load` wait for
the actual load/state result. `status`/`stats` use in-memory snapshots, avoiding
per-second SD writes. Single-client serialization means queries wait behind a
load/state command or update until it completes. Close modal text dialogs
manually before remote game changes; ordinary library/browser/game menu loops
are serviced, but nested system keyboard/confirmation applets are not.

`set` currently allows `frameskip=...`, `clock=...`, `o:frameskip_*=...`,
`slider=N`, and `gldbg=N`. `set rtt=auto|old|new` repeats the previous load cleanly.
Thread and other options require a new `load`;
unsafe live changes are rejected. Use `load` with the same game/state to compare
those. Per-run `o:` values use the existing core option mechanism.

The server rejects peers outside the console's attached IPv4 subnet. It does not
implement internet discovery, port forwarding or shell execution. Plain `get NAME`
accepts only results.tsv/results.md and safe PNG basenames inside the bench
folder. Removing the opt-in is the normal way to disable it; normal users see no
debug UI.

## Files, save states, crash dumps and logs

After a crash, reboot and start PSX3D: everything below works from the library,
without ftpd. All file commands need the token and run on the network worker.
They are refused (`busy`) while a game, load or batch runs, so SD access never
competes with the emulator's own CD or save reads; `quit` to the library first.

```sh
./psx3dctl.py new ls sdmc:/3ds/PSX3D/states
./psx3dctl.py new get sdmc:/3ds/PSX3D/config.ini -o config.ini
./psx3dctl.py new put my-cheats.cht "sdmc:/3ds/PSX3D/cheats/Crash Bandicoot (USA).cht"
./psx3dctl.py new states Silent_Hill
./psx3dctl.py new get-state Silent_Hill 2 -o sh.st2 --thm   # also writes sh.st2.thm
./psx3dctl.py new crashes                                   # Luma dumps
./psx3dctl.py new logs                                      # log.txt .. log.4.txt
devtools/luma-decode.py dump.dmp --elf PSX3D.elf            # decode, symbolise PC/LR
```

- `ls <sdmc:/path>` lists name, `dir`, size and FAT `mtime` (seconds), up to
  about 64 KiB of JSON (`truncated` says if more exist).
- `get <sdmc:/path>` reads **any** file on the SD card, read-only, streamed in
  8 KiB chunks (any size; the host writes `<out>.part` and renames when complete).
- Paths must start with `sdmc:/`; repeated slashes are folded, `.`/`..`,
  backslashes and `:` in components are refused, so nothing outside the card is
  reachable.
- `put <sdmc:/path> <size>` followed by exactly `size` raw bytes (at most 8 MiB)
  writes only below `sdmc:/3ds/PSX3D/{states,cheats,saves,covers,recordings,games}/`
  or to `switches.txt`, `config.ini` and `games.ini`. Never `Nintendo 3DS/`,
  `luma/`, `roms/` or anything else. It writes `<path>.tmp`, moves an existing
  file to `<path>.bak`, renames the new file in (FAT cannot rename over a file),
  restores the backup if that fails, then deletes it. Missing folders below the
  allowed root are created.
- `states <Game_Title>` lists slots 0-9 and `auto` with size, thumbnail size and
  the thumbnail's save time. `get state <Game_Title> <0-9|auto> [thm]` returns
  `<game>.stN`/`.sta` or its `.thm`. Titles use `_` for spaces and match the
  states folder's file names: exact (case-insensitive) first, else one unique
  substring. The host tool converts spaces itself.
- `crashes` lists `sdmc:/luma/dumps/arm11` and `arm9` `.dmp` files; fetch them with
  `get`. `devtools/luma-decode.py` runs the Luma parser and resolves PC and LR with
  `arm-none-eabi-addr2line` against the ELF of the build that crashed.
- Every PSX3D start rotates the log: `log.txt` is the current session,
  `log.1.txt` the previous one (the crashed session after a reboot), up to
  `log.4.txt`. `logs` lists them.

## LAN CIA update and recovery

```sh
./psx3dctl.py old update http://192.168.1.225:8765/PSX3D.cia size=BYTES sha256=64_HEX_DIGITS
```

`update=` in switches.txt is a secondary startup entry point, enabled only with
`remote token=...` in that same file. It accepts the URL
alone; use the control command/fastbench driver to supply size/SHA256. Updating
requires the installed PSX3D CIA. A 3DSX session rejects it instead of updating
some other running title. No update menu is shown to normal users.

The updater accepts numeric private-LAN `http://` URLs, requires a 200 response
and Content-Length, caps downloads at 64 MiB, stages to SD, verifies the complete
size and optional SHA256, then asks AM to inspect the staged CIA. Both the
running title and CIA must be **000400000C3D3D00**; installation targets SD only.
It unloads the game first and never deletes a title or ticket. Failures before
AM starts leave the title untouched. During installation, failures cancel the
AM import; keep a working CIA/FBI recovery path available and do not power off,
remove SD or press HOME. Hashes validate transfer integrity; a token and trusted
LAN/host are still needed because plain HTTP is not authenticated transport.

Self-overwrite follows FBI's installer: it specifically skips title/ticket
deletion when replacing its own running title before `AM_StartCiaInstall`.
After successful AM writes/finish, PSX3D completes the response, tears down the
network/core/video normally and requests libctru's `aptSetChainloaderToSelf()`.
No `svcBackdoor`, raw CP15 writes, reboot, or system-title operations are used.
The on-card `update=` is consumed before installation to prevent a relaunch loop.
Hardware self-update/relaunch passed on both consoles during the remote-loop
acceptance session. If AM or APT refuses it, the host reports failure or a relaunch timeout;
restart manually or install the archived CIA with FBI. An interrupted install
has the same recovery considerations as any CIA replacement.

References checked during implementation:

- [FBI self-update](https://github.com/Steveice10/FBI/blob/master/source/fbi/update.c)
- [FBI install: skip deletion for the running title](https://github.com/Steveice10/FBI/blob/master/source/fbi/action/installcias.c)
- [libctru APT chainloader API](https://github.com/devkitPro/libctru/blob/master/libctru/include/3ds/services/apt.h)
- [libctru AM API](https://github.com/devkitPro/libctru/blob/master/libctru/include/3ds/services/am.h)

## Local validation

Run `psx3d/tests/dev/run.sh` with a host C compiler, Python 3, pkg-config,
libpng and OpenSSL development libraries. The ASan/UBSan tests cover the actual
controller and switch parser, loopback TCP authentication/framing, screenshots,
batch timing and cancellation, and updater validation and recovery. These tests
do not contact consoles. See [building and testing](building.md) for other checks.

For hardware comparisons, keep the same state, clock, frameskip and network
conditions. Use a baseline/candidate/baseline bracket, save the exact requests,
check build identities after installs, and inspect screenshots. Do not overwrite
benchmark slots; use a separate spare slot for save/load checks. Keep exclusive
ownership of each console for the whole session.

Self-updates on New 3DS can take up to ten minutes. Wait for a changed session
before declaring a timeout; never send a second install while the first may
still be writing. CIA title counters must not decrease. If installation or
relaunch is uncertain, stop automatic control and inspect the console before
attempting recovery.

### Provision a new benchmark state (developer only)

`state-import TITLE slot=N size=4456448 sha256=HEX` accepts one native raw HLE
state for a game already in the library. This command requires the on-card
`remote` + token pair, an idle library and the console's benchmark slot (Old 2,
New 0). The original six benchmark games are always rejected. Every existing
state is preserved, including slots belonging to newly added games.

The client waits for a framed JSON `ready` response before sending exactly
4,456,448 binary bytes. A second framed JSON response confirms the import.
The worker stages the transfer under `sdmc:/3ds/PSX3D/states/`, checks its exact
length, current raw-state header/version, HLE flag and mandatory SHA256, then
creates the final slot exclusively. It never accepts a destination path,
loads a state, calls the core, or installs/relaunches the app. A disconnected
or invalid transfer removes its temporary file. A power loss can leave an
`.import` file or a partial newly created slot; inspect those manually before
retrying. There is deliberately no overwrite or delete command.

`tools/bench-pool/import-state.py` implements this handshake, throttles at
384 KiB/s, holds `devtools/fastloop/lock.old|new`, checks the unavailable marker
and an explicit expected build identity, and archives status plus a library
screenshot. Example (after the coordinator installs a build with this command):

```
python3 tools/bench-pool/import-state.py new 'Spyro the Dragon' scene.st0 \
  --expected-build BUILD_ID --out /path/to/new-evidence
```

The format check validates the state envelope, not every internal core field
or the disc identity. Use only locally generated, hash-verified scene files
paired with the exact disc revision recorded in their manifest.
