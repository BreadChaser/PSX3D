# PSX3D core

This is PSX3D's modified fork of [PCSX-ReARMed](https://github.com/libretro/pcsx_rearmed),
originally vendored from commit `c8816799b50388e61cfe237fe2cdbb7d8175f20a`
with PSX3D's two existing patches applied. It is maintained directly in this
repository; there is no submodule or patch regeneration step and no planned
upstream synchronization.

The included arm-mem, libchdr (including miniz, LZMA and Zstandard) and
libretro-common dependencies were already plain files at that revision.
The unused frontend/libpicofe submodule is not required by the 3DS build.

The original GPL licence, AUTHORS, dependency licences and copyright notices
are retained. The historical core version strings are fixed to their original
values so this housekeeping does not change the executable. See
[building and testing](../docs/building.md) for build and identity checks.

Non-3DS frontend/platform source, other CPU implementations, and upstream
CI, packaging and tests have been removed. Shared frontend glue still compiled
by the libretro host and headers included by it are retained. The build targets
are supported for 3DS only; inactive legacy Makefile branches remain to keep
the established simulator setup compatible.

Unused GLES, P.E.Op.S., NEON and null-SPU plugins, hardware audio drivers and
unused CD plugin implementation were removed. The portable P.E.Op.S./NEON
files still used as independent host test oracles live under
`tools/gpu-compare/reference/`; they are never linked into PSX3D.
`plugins/cdrcimg/cdrcimg.h` remains because shared frontend code includes it.

Unused lightrec/lightning/mman implementations, ARMv7 memory routines,
libretro-common subsystems and uncompiled dependency sources/assets were
removed using the clean 3DS compiler dependency manifests. All standalone
licence/author notices remain. Required ARMv6 memory routines, VFS/threading,
CHD codecs (miniz, LZMA, FLAC, Huffman and Zstandard), compressed disc support,
cheats and patch support remain. Included lightrec declarations are retained
because shared ARM code still preprocesses them.

Unused standalone plugin Makefiles, the upstream GPULib test driver and SDL
output were also removed; root make still builds GPULib directly.
