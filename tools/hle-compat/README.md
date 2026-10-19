# HLE BIOS compatibility probes

The shipping BIOS is HLE. The optional real BIOS is a reference supplied by the
user; no BIOS, commercial executable, save state or captured game data is included.
All generated output belongs under ignored `devtools/hle-compat/`.

`build.sh` builds a **private copy of the current checkout's core** through the
existing gpu-compare builder. Use a fresh `BUILD_DIR` for another core revision:
the capture builder deliberately reuses an existing `core_capture` directory.
It uses the test-only P.E.Op.S. framebuffer oracle to inspect BIOS behavior;
it does not add a software-renderer feature to PSX3D.

```sh
source /home/tony/Development/PS1_3ds/devtools/env.sh
BUILD_DIR=/home/tony/Development/PS1_3ds/devtools/hle-compat/repro tools/hle-compat/build.sh
python3 tools/hle-compat/test-cards.py \
 --build /home/tony/Development/PS1_3ds/devtools/hle-compat/repro \
 --out /home/tony/Development/PS1_3ds/devtools/hle-compat/repro-cards \
 --bios /home/tony/Development/PS1_3ds/devtools/bios/ps-22a.bin
```

Requires the project's ARM Linux compiler, qemu-arm-static, clang and ld.lld.
Omit `--bios` to test HLE alone. `--qemu` selects a different ARM emulator.
The test does not contact a console or open a user's memory card.

The small freestanding MIPS programs call the actual A0/B0 BIOS entrypoints.
Seven disposable card layouts cover single-block, adjacent and fragmented
multi-block files, insufficient/full cards and the last block. Each layout
runs both create/read/list and create/read/list/delete. Assertions cover
completion, failure without mutation, existing-file preservation, data readback,
aligned reads/writes crossing a block boundary, reported size and final card
bytes. HLE and real BIOS cards must match except for the real BIOS's extra
backup MC header in the last sector of block 0.

The audit runner accepts `game frames state|- options.ini [last.ppm]` and:

- `SHOTS=/directory`: PPM screenshots every 600 frames and on the final frame.
- `INPUT_SCRIPT=/file`: inclusive `first last hex-mask` libretro joypad intervals,
  starting at frame 1; `8` is Start, `1` is Cross, `0x20` is Down.
- `DUALSHOCK=1`: attach the DualShock device (required by Ape Escape).
- `CARD=/private/card.mcd`: load/save the 128 KiB libretro card. Missing file
  starts with the core's formatted card.
- `FINAL_STATE=/private/state`: save an optional continuation state.
- `BOOT_FRAMES=N`: warm up before the measured loop. Input frame numbering
  restarts at 1 for the measured loop, as in gpu-compare's runner.
- `AUDIO=/private/audio.pcm`: optional raw stereo signed 16-bit/44.1 kHz PCM.

Logs include cumulative audio activity/hash and 64-bit RAM checkpoints. Audio
activity establishes that samples exist; it does not certify audible quality.
HLE and real BIOS have different startup timing: compare the reached scene,
not equal frame numbers or whole-RAM hashes across the two BIOS implementations.
The six-state before/after regression gate separately compares exact draw hashes
and RAM checkpoints with the same BIOS and inputs.
