# Widescreen catalog

`catalog.json` audits the 225 unique serial/boot-executable identities found
in the local and read-only NAS collections. It contains no ROM paths or game
bytes. `collection` marks the first occurrence (local takes priority).

Runtime files are complete toggle bundles, matched to both serial and the
boot executable. Each carries per-console availability. Staged candidates in
`research/` are never scanned at runtime. The catalog and `docs/widescreen.md`
record technical acceptance separately from source provenance.

Acceptance requires the actual disc revision, all required projection,
frustum and HUD operations together, gameplay and edge-picture checks,
rollback, and hardware cost. Old 3DS hides a bundle after >5% native fps loss
or loss of previously full game speed with automatic frameskip. Crash 1 and
Spyro 1 are hidden because of incomplete culling and a crash warning,
respectively. 2D-only presentations stay hidden; unassessed discs retain the
GTE fallback, which is not counted as curated native coverage.

Third-party widescreen/culling data is included under the user's source policy.
Major sources are credited once in `tools/art/CREDITS.md`; per-code credits
are not a release gate. Libretro-database adaptations retain their required
CC BY-SA 4.0 attribution and full licence. Other sources are not relabelled
with that licence or with DuckStation's processing-script MIT licence.

## Bundle format and revision matching

Runtime data lives at `romfs:/widescreen/SERIAL-HHHHHHHH.ws`. The serial is the
normalized nine-character disc ID. `HHHHHHHH` is FNV-1a (offset 2166136261,
multiplier 16777619, modulo 2^32) over the 2048-byte PS-X EXE header followed
by its declared text bytes, read from disc. The core caps text size at 2 MiB.
No file is selected by game title, cheat description or an executable patched
in RAM. Serial-only files are reserved for deliberately revision-independent
entries; none is shipped in this branch.

```
PSX3D-WS-1
aspect=16:9
models=1,1
[complete bundle]
A7012340 10000C00
```

`models=Old,New` takes one value per console: 0 hides Widescreen (no usable
fix), 1 offers it, 2 offers it with a "can lower the speed" warning in
Settings, and 3 holds it back to keep the speed up (Settings says so instead
of "no widescreen fix"). Hidden values never silently substitute GTE. Presets may independently hide the
feature. Both permissions must allow it. Aspect is fitted into 400×240.

The interpreter accepts `30` byte, `80` halfword and `90` word writes;
`D0` equality skips one following write; `A4` word equality guards a block
terminated by `00000000 FFFF`; `A7` compares the original high halfword of its
value and replaces it with the low halfword. Addresses are physical 2 MiB
PS1 RAM offsets in the opcode's low 24 bits. Misaligned/unsupported/malformed
operations reject the entire file. Comments begin with `#` and are provenance,
not executable metadata. No include, script or arbitrary SD path is accepted.

All candidate writes use `A7` with known originals so that a loaded patched
state can be cleaned when Widescreen is off. Do not add blind writes without
solving their state-load rollback too. Overlay-specific changes need explicit
`A4` guards. Replaced game/overlay values are preserved on toggle-off. The
normal cheat list and `.on` persistence never own this bundle's entries.
