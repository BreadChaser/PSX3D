# Builds the PCSX-ReARMed core as a static library, then the PSX3D app.
.PHONY: all core app cia cxi clean sdk-check host-tests

all: app

# The private citro3d code (psx3d/source/renderqueue.c, gpu_cache.c) needs the pinned SDK:
# stop before compiling anything when libcitro3d, its headers or libctru differ from
# tools/build/toolchain.lock. SDK_CHECK=0 skips it (deliberate SDK upgrades only).
SDK_CHECK ?= 1
sdk-check:
	@python3 tools/build/toolchain.py check --abi
ifeq ($(SDK_CHECK),1)
core: sdk-check
endif

# Every host check that needs no ROM, BIOS or console (what CI runs).
host-tests:
	bash tools/build/host-tests.sh

# GPU-only frontend.
# Renderer-only compile/link experiments; prepare.py invalidates changed objects.
PICA_LAYOUT ?= split
ifeq ($(filter $(PICA_LAYOUT),baseline split section os),)
$(error PICA_LAYOUT must be baseline, split, section, or os)
endif
PICA_LAYOUT_CFLAGS :=
PICA_OPT :=
ifneq ($(filter $(PICA_LAYOUT),split section),)
PICA_LAYOUT_CFLAGS += -DPICA_LAYOUT_SPLIT
endif
ifeq ($(PICA_LAYOUT),section)
PICA_LAYOUT_CFLAGS += -DPICA_LAYOUT_SECTION
endif
ifeq ($(PICA_LAYOUT),os)
PICA_OPT := -Os
endif
export PICA_LAYOUT PICA_LAYOUT_CFLAGS PICA_OPT

EXTERN_SYMS := psx3d_depth_get psx3d_depth_frame psx3d_overlay_get psx3d_ov_seq psx3d_set_strength \
	psx3d_right_get psx3d_stats psx3d_set_half3d psx3d_prof_t psx3d_prof_c psx3d_prof_on new_dynarec_print_stats \
	psx3d_irq_t psx3d_irq_c CdromId
EXTERN_SYMS += CdromExeHash psx3d_widescreen_hook psx3d_widescreen_state_hook psx3d_patch_clear
EXTERN_SYMS += Mcd1Data Mcd2Data psx3d_clip_prepare padAnalogMode padToggleAnalog
EXTERN_SYMS += SaveFuncs LoadMcd
# core-internal calls that go to PSX3D (the core's code stays as it is)
WRAP_SYMS := LoadMcd
EXTERN_SYMS += psx3d_idle_skip psx3d_idle_pointer psx3d_idle_delay
EXTERN_SYMS += psx3d_spu_worker_ticks psx3d_spu_worker_items psx3d_skip pica_display_changed
# Real 3D (psx3d/source/gpustereo.c): the frontend takes over two core hooks
# (--wrap inside the core's partial link; -u __real_ keeps the originals in place)
# and reads the GTE depth table and gpulib's drawing registers.
WRAP_SYMS := psx3d_depth_cmd psx3d_stereo_set
EXTERN_SYMS += $(WRAP_SYMS) $(addprefix __real_,$(WRAP_SYMS)) psx3d_stereo_enabled psx3d_ztable psx3d_zframe gpu
EXTERN_SYMS += pica_init pica_exit pica_game_begin pica_game_end pica_flush_vertices pica_frame_started pica_frame_ended \
	pica_texmask pica_texmemo psx3d_skipgate pica_thread_call pica_thread_sync pica_state_lost pica_prepare_present pica_picture pica_stats pica_probe pica_debug pica_submit pica_prims pica_pmu pica_open_frame pica_async pica_double_buffer pica_wait_frame pica_frame_pending pica_dump_vram

core:
	@python3 tools/pica-layout/prepare.py $(PICA_LAYOUT)
	$(MAKE) -C core -f Makefile.libretro platform=ctr CTR_GPU=pica EXTRA_EXTERN_SYMS="$(EXTERN_SYMS)" EXTRA_WRAP_SYMS="$(WRAP_SYMS)"

# Presets and defaults.ini (docs/settings.md section 6): a changed default stops
# the build until every tested preset pins it or is retested.
app: core
	@python3 tools/presets/check.py
	$(MAKE) -C psx3d

# Installable CIA. Needs makerom (Project_CTR) and bannertool on PATH,
# or MAKEROM=/path/to/makerom BANNERTOOL=/path/to/bannertool.
MAKEROM    ?= makerom
BANNERTOOL ?= bannertool
# CIA title version from PSX3D_VERSION. The HOME Menu only refreshes a
# cached icon when this number changes. makerom drops the minor field
# (bits 4-9) of the title version, so major*16+minor goes in the major field
# and patch in the micro field. Reserve micro 0..2 for the private pre-release
# diagnostics (0.3.1/0.3.2). Keep the offset for monotonic public updates:
# v0.3.0 = 3075, v0.3.1 = 3076, v0.4.0 = 4099, v0.4.1 = 4100, v0.5.0 = 5123.
VERSION    ?= $(shell sed -n 's/.*PSX3D_VERSION *"\([0-9]*\)\.\([0-9]*\)\.\([0-9]*\)".*/\1 \2 \3/p' psx3d/source/psx3d.h | awk '{print ($$1*16 + $$2)*1024 + $$3 + 3}')

cia: app
	$(BANNERTOOL) makebanner -i psx3d/meta/banner.png -a psx3d/meta/banner.wav -o psx3d/build/banner.bnr
	$(BANNERTOOL) makesmdh -s "PSX3D" -l "PSX3D - PS1 emulator for 3DS" -p "BreadChaser" \
		-i psx3d/meta/icon.png -o psx3d/build/icon.icn
	$(MAKEROM) -f cia -o psx3d/PSX3D.cia -elf psx3d/PSX3D.elf -rsf psx3d/meta/app.rsf \
		-icon psx3d/build/icon.icn -banner psx3d/build/banner.bnr -target t -ver $(VERSION)

# The executable container inside the CIA. The CIA carries a randomized ticket, so two
# CIAs of one build differ; two CXIs of one commit are byte-identical (docs/building.md).
cxi: cia
	$(MAKEROM) -f cxi -o psx3d/PSX3D.cxi -elf psx3d/PSX3D.elf -rsf psx3d/meta/app.rsf \
		-icon psx3d/build/icon.icn -banner psx3d/build/banner.bnr -target t -ver $(VERSION)

clean:
	$(MAKE) -C core -f Makefile.libretro platform=ctr CTR_GPU=pica EXTRA_EXTERN_SYMS="$(EXTERN_SYMS)" clean
	$(MAKE) -C psx3d clean
