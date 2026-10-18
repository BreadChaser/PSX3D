# Core ownership and pruning

PSX3D now maintains its modified PCSX-ReARMed core as ordinary files under
`core/`. No submodule, patch application or patch regeneration is required.
The source origin is libretro/pcsx_rearmed at
`c8816799b50388e61cfe237fe2cdbb7d8175f20a`, with both PSX3D patches from
public main `2afe172` applied by the original `make patch` target.

## Vendoring

The first commit imports the complete patched source, including the already
embedded arm-mem, libchdr and libretro-common dependency trees. The uninitialized
libpicofe gitlink was unused by the 3DS build and is omitted. GPL COPYING, AUTHORS,
dependency licences and notices are retained. Obsolete patch-writing helpers
were removed; historical reports explicitly identify their legacy instructions.
The shared ignored main-checkout mk.sh is untouched; the isolated build wrapper
lives in ignored `devtools/vendor-core/`. Use plain `make` in this checkout.

Only two imported source files differ from the patched baseline: the Makefiles
now generate fixed historical `GIT_VERSION` and `REV` values instead of querying
a removed nested Git repository. The unused nested .gitmodules is removed.
All implementation files match their original SHA256 manifest.

## Identity procedure

Clean builds use Tonymini devkitARM GCC 16.1.0 and the same installed devkitPro
libraries, `PICA_LAYOUT=split`, runtime dual renderers, and CIA title counter
3076. The original baseline was built from `2afe172` with the original patch
workflow before removing the gitlink. Full ELF/3DSX/CIA, maps, logs, source
manifests and comparison scripts are archived in ignored
`/home/tony/Development/PS1_3ds/devtools/vendor-core/`.

An uncontrolled rebuild differed from release RC2 only in five bytes of the
`build time:` log string in `.rodata`. Set `SOURCE_DATE_EPOCH=1792295667`
(2026-10-18 03:54:27 UTC, RC2's embedded time) for reproducible comparisons.
With that environment, the original patched baseline and the vendored build
have identical allocated ELF sections, addresses, flags and alignment,
including `.text`, `.rodata`, `.data`, exception tables, constructors, GOT,
BSS and TLS. Both also match the immutable RC2 ELF's allocated sections.
The complete 3DSX is byte-identical to RC2. ELF debug paths identify this
worktree; nonallocated debugging information is not an executable change.

CIA packaging is not deterministic: makerom seeds its random generator from
`time(0)`, then generates ticket material, ticket ID and content IDs (makerom
`src/utils.c` and `src/cia.c`). Differences are confined to the ticket and TMD,
including their signatures/hashes and content ID; the complete NCCH content
from CIA offset `0x3900` through EOF is byte-identical to RC2. Title counter
remains 3076. The complete 3DSX is identical too.

## Pruning and final results

Pruning stages and final size measurements are recorded here after each clean
build and identity check. No simulator or console result is claimed yet.

### Stage 1: unused platforms and CPU implementations

Removed 97 files: Android JNI, Maemo/Miyoo and other non-3DS frontend assets
and glue, AArch64/NEON GTE and AArch64 dynarec sources, unused debugger/socket
sources, upstream CI/configure/packaging/test tools. Retained the libretro
export list/version script and shared frontend source/headers used by 3DS.
Clean dual-renderer CIA build passes. All allocated ELF sections and the
complete 3DSX remain identical to the vendored baseline.

### Stage 2: unused plugins

Removed the GLES/null-SPU plugins, unbuilt CD plugin implementation and
non-3DS audio drivers/DSP code. Moved 14 unmodified portable P.E.Op.S./NEON
oracle files into `tools/gpu-compare/reference/` and removed the remaining
unused plugin code. Updated the host build script to assemble their original
relative include layout outside the repository; oracle source bytes and
copyright notices are unchanged. GPULib, gpu_unai (including its selectable
old renderer) and gpu_pica are retained.

Clean dual-renderer CIA build passes. All allocated ELF sections and the
complete 3DSX remain identical to the vendored baseline.
