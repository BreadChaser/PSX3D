# Builds the PCSX-ReARMed core as a static library, then the PSX3D app.
.PHONY: all core app cia clean

all: app

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
EXTERN_SYMS += Mcd1Data Mcd2Data psx3d_clip_prepare padAnalogMode
EXTERN_SYMS += psx3d_idle_skip psx3d_idle_pointer psx3d_idle_delay
EXTERN_SYMS += psx3d_spu_worker_ticks psx3d_spu_worker_items psx3d_skip pica_display_changed
EXTERN_SYMS += pica_init pica_exit pica_game_begin pica_game_end pica_flush_vertices pica_frame_started pica_frame_ended \
	pica_texmask pica_texmemo psx3d_skipgate pica_thread_call pica_thread_sync pica_state_lost pica_prepare_present pica_picture pica_stats pica_probe pica_debug pica_submit pica_prims pica_pmu pica_open_frame pica_async pica_double_buffer pica_wait_frame pica_frame_pending pica_dump_vram

core:
	@python3 tools/pica-layout/prepare.py $(PICA_LAYOUT)
	$(MAKE) -C core -f Makefile.libretro platform=ctr CTR_GPU=pica EXTRA_EXTERN_SYMS="$(EXTERN_SYMS)"

app: core
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
# v0.3.0 = 3075, v0.3.1 = 3076, v0.4.0 = 4099.
VERSION    ?= $(shell sed -n 's/.*PSX3D_VERSION *"\([0-9]*\)\.\([0-9]*\)\.\([0-9]*\)".*/\1 \2 \3/p' psx3d/source/psx3d.h | awk '{print ($$1*16 + $$2)*1024 + $$3 + 3}')

cia: app
	$(BANNERTOOL) makebanner -i psx3d/meta/banner.png -a psx3d/meta/banner.wav -o psx3d/build/banner.bnr
	$(BANNERTOOL) makesmdh -s "PSX3D" -l "PSX3D - PS1 emulator for 3DS" -p "BreadChaser" \
		-i psx3d/meta/icon.png -o psx3d/build/icon.icn
	$(MAKEROM) -f cia -o psx3d/PSX3D.cia -elf psx3d/PSX3D.elf -rsf psx3d/meta/app.rsf \
		-icon psx3d/build/icon.icn -banner psx3d/build/banner.bnr -target t -ver $(VERSION)

clean:
	$(MAKE) -C core -f Makefile.libretro platform=ctr CTR_GPU=pica EXTRA_EXTERN_SYMS="$(EXTERN_SYMS)" clean
	$(MAKE) -C psx3d clean
