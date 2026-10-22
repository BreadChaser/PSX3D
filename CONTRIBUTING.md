# Contributing

Thanks for helping. Bug reports with a log are the most useful thing you can send.

## Reporting a bug

Open an [issue](https://github.com/BreadChaser/PSX3D/issues/new/choose) and fill in the form. We need:

- Your console: Old 3DS, Old 3DS XL, Old 2DS, New 3DS, New 3DS XL or New 2DS XL
- The PSX3D version (shown in the top corner of the library)
- The game, its region and, if you can, its serial (for example SCUS-94900)
- What happened, what you expected, and how to make it happen again
- `sdmc:/3ds/PSX3D/log.txt`

About the log:

- PSX3D keeps the last five sessions as `log.txt` (newest), `log.1.txt` … `log.4.txt`.
- Copy the right file before you start PSX3D again, or pick the one from the session where it went wrong.
- For a drawing problem, a photo or screenshot of the screen helps a lot.
- Don't send game images, BIOS files or save files you don't own.

## Asking about a game

Check [docs/compatibility.md](docs/compatibility.md) and the [known issues](README.md#known-issues) first.
A game that isn't listed hasn't been tested. Tell us how it ran and we will add it.

## Changing code

- Open an issue first for anything bigger than a small fix.
- The emulator core is in `core/` and is edited directly (it started as PCSX-ReARMed and keeps its GPL licence and credits).
- The app is in `psx3d/`. Build steps and tests: [docs/building.md](docs/building.md).
- Run the host tests before you send a change: `make host-tests`.
- Keep a change to one thing, with a message that says what and why.

**Speed on the Old 3DS depends on where the code sits in memory.**
A small, harmless-looking change can move hot code and cost a few percent.

- Only a measurement on a real Old 3DS decides if a speed change is good.
- A PC simulator tells you the output is the same. It can't tell you the speed.
- Measure before, after and before again (A/B/A) in the same scene, and report the three numbers.
- See [performance](docs/performance.md) for how we measure.

The core layout check (`tools/layout/check.sh`) must pass. It stops a frontend change from moving the core.

## Game settings and widescreen

- Per-game settings live in `psx3d/romfs/presets/` ([how they work](docs/presets.md)).
- Every value needs a source and a confidence. Say which console, disc and scene you tested.
- Don't copy a setting from another region's disc without testing it.
- Widescreen fixes are described in [docs/widescreen.md](docs/widescreen.md).

## Licence

PSX3D is GPL-2.0. By sending code you agree it is released under the same licence.
