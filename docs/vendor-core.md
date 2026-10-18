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
The complete 3DSX is byte-identical to RC2. All six controlled builds within
this worktree also have byte-identical **complete ELF files**, including debug
information. RC2's ELF has different nonallocated debug paths; its allocated
sections and the complete packaged executable content match exactly.

CIA packaging is not deterministic: makerom seeds its random generator from
`time(0)`, then generates ticket material, ticket ID and content IDs (makerom
`src/utils.c` and `src/cia.c`). Differences are confined to the ticket and TMD,
including their signatures/hashes and content ID; the complete NCCH content
from CIA offset `0x3900` through EOF is byte-identical to RC2. Title counter
remains 3076. The complete 3DSX is identical too.

## Pruning and final results

Each resulting source tree passed a clean build and the identity checks below.

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

### Stage 3: unused dependencies

Removed 953 dependency/header files absent from the clean 3DS dependency
manifests: lightrec/lightning/mman implementations, ARMv7 memory code,
unused libretro-common audio/video/network/media/test/sample subsystems and
uncompiled libchdr dependency sources/assets. Every standalone licence and
AUTHORS notice remains, including those belonging to removed implementations.
The remaining shared lightrec declarations are included by ARM core code.

CHD codecs, miniz-based compressed image handling, libretro VFS/thread/CPU
features, cheats and PPF support remain. Clean dual-renderer CIA build passes;
all allocated ELF sections and the complete 3DSX match the vendored baseline.
The frontend host suite and developer-control/transport/updater suites pass
with ASan/UBSan. Generated host mockups were archived under devtools and the
tracked reference images restored.

### Stage 4: remaining standalone plugin build/test glue

Removed six unused files: the standalone gpu_unai/GPULib Makefiles, GPULib
plugin make fragment, upstream GPULib test driver and SDL video output.
Root make compiles the shared GPULib implementation directly. Clean CIA
build and complete ELF/3DSX identity checks pass.

## Final measurements

Counts below are exact Git-tree snapshots, not build-directory sizes. Text
lines count newline characters in files without NUL bytes; byte counts include
all tracked files. The last pruning commit is `cf54356`; subsequent changes
only clarify documentation. The vendored baseline is `eaaa0ea`.

- Vendored baseline: core **1,392 files / 741,010 text lines / 25,851,020 bytes**.
- Platform/CPU pruning (`57e7988`): core 1,295 files / 719,004 lines / 24,973,144 bytes.
- Plugin pruning (`98fd180`): core 1,243 files / 669,750 lines / 23,186,262 bytes.
- Dependency pruning (`cf54356`): core **290 files / 178,557 lines / 6,371,070 bytes**.

Core reduction: **1,102 files, 562,453 lines and 19,479,950 bytes (75.35%)**.
The whole tracked repository, including the relocated host oracles and all
frontend/docs/assets, goes from **3,713 files / 39,802,710 bytes** to
**2,626 files / 20,985,281 bytes** at the last pruning commit: **47.28% smaller**.
Whole-repository text lines drop from 881,728 to 337,784. These are checkout
contents: Git history still retains the original vendoring commit and deleted
sources. This work does not rewrite history or claim existing clones shrink.

## Final verification and evidence

All of `baseline-controlled`, `vendored`, `prune-platform`, `prune-plugins`,
`prune-deps` and post-commit `final` clean builds pass and produce:

```text
Complete ELF SHA256 (same worktree, all six builds):
2e6ac50e4cea912f42fc44a8a954872a81b928bd4bc8f8fe422c39977e621bc9
Complete 3DSX SHA256 (all six builds AND immutable release RC2):
0a8f07efdaa20911786a221a4d4a61bf4caa87613c68952fcad14cd610b02f59
.text   2,146,052 bytes  SHA256 295e5508f2acb083915f9746c53840242a85499907284b537362a42b0bfc99fc
.rodata   329,324 bytes  SHA256 8bb8dba2cd784d7293017b20fc248e3cbd46b349bad815811442448d492a7eef
.data     114,404 bytes  SHA256 c173a296a444fe3019c2ab6ce922a39fe92a3016f199dda92656a55e4f677d01
```

The entire CIA NCCH content is also identical to release RC2 in every build
(SHA256 `51ce42247ab85dac4b3e91768c80877fddba2ade26d6ac90ad7f1b6dd44533a2`).
The randomized ticket/TMD metadata explains the differing outer CIA hashes.
No executable implementation, data, layout, title version or RomFS changes.

The relocated P.E.Op.S./portable NEON reference renderers compile, as does the
full Linux ARM command-capture core/runner with the pruned dependencies. The
existing synthetic fixture command with `--check` completes successfully.
It still reports its existing polygon-sampling and approximate mask-half-blend
picture differences; this is harness validation, not a new accuracy claim.
The renderer polygon/presentation/held-frame regression passes with ASan/UBSan.
Frontend host checks and developer-control/parser/transport/updater tests pass
with sanitizers (including all seven Python client tests). Those mocked
"old/new" tests use localhost; they do not contact either console.

**600-frame six-game draw-hash comparisons: not run. Old 3DS A/B/A: not run.**
Both were conditional on an executable change, and the stronger full-3DSX and
allocated-ELF identity checks pass. No Old or New 3DS connection, install,
input, state write or NTR change was made. RC2 was left untouched, so no
restoration was needed. No new hardware performance numbers are claimed.

Evidence in ignored `devtools/vendor-core/` includes `build.sh`, `mk.sh`,
`compare-elf.py`, original patched-source archive/manifest, per-stage
ELF/3DSX/CIA/maps/build logs/SHA256 lists/identity JSON, exact deletion lists,
Git-tree size snapshots, source/notice/oracle audit, host/developer/sanitizer
logs and host reference fixture output. None of the test output is committed.

## Deliberately retained

- Both gpu_unai implementations and gpu_pica/GPULib, the interpreter fallback,
  ARM dynarec/GTE code, sound and CD threads, HLE/real BIOS handling, cheats,
  PPF, PBP/compressed image support, and all CHD codecs used by the build.
- libretro frontend/shared declarations and VFS/threading helpers: PSX3D is a
  libretro host even though it does not use RetroArch. Removing these would
  remove shipped features or require an implementation change.
- Included lightrec/NEON declarations and unused conditional branches inside
  shared implementation files. They are harmless declarations/inactive paths;
  stripping them would extend this cleanup into source refactoring.
- Legacy conditional Makefile branches and the two `ctr` insertion markers.
  Existing ARM simulator preparation tools depend on that structure. Only the
  historical revision generation changed; broad Makefile rewriting was not
  needed to remove dead source and would add avoidable build risk. Unsupported
  legacy platform targets are not offered as working builds of this fork.
- Standalone licence/AUTHORS notices, upstream historical credit documents and
  14 portable renderer files used as independent host-only test oracles.

Commits are local on `t3/vendor-core`; no push, tag or release was performed.
