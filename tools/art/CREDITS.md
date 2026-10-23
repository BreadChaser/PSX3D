# Art sources

| File | Source | License |
|---|---|---|
| palm_single.svg | "Palm Tree" by Algot Runeman (runeman.org) | CC0 |
| palm_bushy.svg, palms_pair_a.png, palms_pair_b.png | palm tree silhouettes | CC0 |
| Michroma-Regular.ttf | Michroma by Vernon Adams | SIL OFL 1.1 (Michroma-OFL.txt) |

## Cheat codes

`psx3d/romfs/cheats/` is the Sony - PlayStation cheat collection from the
[libretro-database](https://github.com/libretro/libretro-database) (commit
12c21cf, 2026-10-10), licensed under
[CC BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/) by the
libretro contributors. The files are unmodified; the license is included as
`psx3d/romfs/cheats/LICENSE.txt`.

## Box art names

`psx3d/romfs/covers/` (`names.txt`, `serials.idx`) lists the pictures of the
[libretro-thumbnails](https://github.com/libretro-thumbnails/libretro-thumbnails)
"Sony - PlayStation" boxart set and maps disc serials to them, using the redump
names and serials of the [libretro-database](https://github.com/libretro/libretro-database)
"Sony - PlayStation" DAT (CC BY-SA 4.0, libretro contributors; licence in
`psx3d/romfs/covers/LICENSE.txt`). No pictures are shipped; the app downloads
them. `tools/boxart/` builds the table.

## Widescreen bundles and research

- [DuckStation chtdb](https://github.com/duckstation/chtdb): widescreen, culling and HUD patch collection; original code rights remain with their authors (MIT covers its processing scripts).
- [libretro-database](https://github.com/libretro/libretro-database), libretro contributors: selected/adapted PlayStation widescreen data, **CC BY-SA 4.0**, commit `12c21cf79f1e20eff6ac68b80875816c717d0a7a`; GT2 conditional patches by Silent and NFS High Stakes by Hypercycle; complete licence in `psx3d/romfs/widescreen/LICENSE.txt`.
- [CTR-ModSDK](https://github.com/CTR-tools/CTR-ModSDK): GPL-3.0; retail frustum/HUD symbols used for research, no source code copied.
- [REDRIVER2](https://github.com/OpenDriver2/REDRIVER2): MIT; camera/draw research, no source code copied.
- [Gran Turismo 1 OverlayCheat](https://github.com/soaresden/GranTurismo1-OverlayCheat): GPL-3.0; research only.
- [Crash Bandicoot decomp](https://github.com/khasinski/crash-bandicoot-decomp): visibility-list research only; no source code copied.
- [PeterDelta widescreen releases](https://github.com/PeterDelta/PCSX2): PS1 patch research.

Bundled libretro-database adaptations convert code delimiters, add exact disc
and boot-executable identities and guard original values for rollback. Their
CC BY-SA licence and source references are retained with the adapted data.
Other third-party codes are not relabelled MIT or CC BY-SA. See
`docs/widescreen.md` for technical validation and per-console availability.
