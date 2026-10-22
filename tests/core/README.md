These synthetic MIPS programs execute the real production ARMv6 dynarec,
interpreter, GTE and memory handlers under qemu-arm. Run `bash tests/core/run.sh`;
needs gcc-arm-linux-gnueabihf, libc6-dev-armhf-cross and qemu-user. No ROM, BIOS,
network, console, or untracked file is used. The core is copied into a temporary
build; only the Linux platform stanza and relocated RAM/scratchpad mappings
are adapted to match the 3DS. Production sources are never rewritten in place.

- memory.c: 192 directed MFC0/MFC2/CFC2 cases (r0-r31, plain/branch-slot),
  4,000 random load-delay programs, 6,000 ALU/load/store programs, scratchpad
  guards and mirrors, and repeated emitted-store SMC invalidation.
- gte.c: 12,000 random/edge NCCT/NCLIP/MVMVA guest programs, comparing every
  GPR and CP2 register with the interpreter, then repeating production codegen
  for exact cycles and busy-cycle state. FLAG is observed for MVMVA: the
  interpreter does not model dead-FLAG optimizations as an exact oracle.
- determinism.c: generated PS-X EXE, HLE init, 60 frames, save-state restore,
  repeat; every frame's RAM/GPR/CP2 digest and cycle count must match.

Missing tools return SKIP (77). tools/build/host-tests.sh rejects any skipped
suite by default; HOST_TEST_ALLOW_SKIPS=1 explicitly allows a local partial run.
CI installs the prerequisites and forces skips to fail. These are correctness
checks, not a model of real PICA rendering, Old 3DS cache behavior, or threads.
The ROM-based 13-heavy/6-standard/68-HLE gate remains an additional local gate.
