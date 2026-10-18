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
[the vendoring report](../docs/vendor-core.md) for pruning and identity evidence.
