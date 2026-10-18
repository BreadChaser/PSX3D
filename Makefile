# Builds the PCSX-ReARMed core as a static library, then the PSX3D app.
.PHONY: all patch core app cia clean

all: app

# small fixes to the upstream core, kept as patches so the submodule stays upstream
patch:
	@for p in $(CURDIR)/patches/*.patch; do \
		git -C core apply --reverse --check $$p 2>/dev/null || git -C core apply $$p; \
	done

# The frontend always includes both renderers; selection is a game setting.
# Core-only builds still accept CTR_GPU=pica/unai for diagnostics.
ifneq ($(origin GPU),undefined)
$(error GPU= was replaced by the runtime Renderer setting; build with plain make)
endif
EXTERN_SYMS := psx3d_depth_get psx3d_depth_frame psx3d_overlay_get psx3d_ov_seq psx3d_set_strength \
	psx3d_right_get psx3d_stats psx3d_set_half3d psx3d_prof_t psx3d_prof_c psx3d_prof_on new_dynarec_print_stats \
	psx3d_irq_t psx3d_irq_c psx3d_renderer_select CdromId
EXTERN_SYMS += pica_init pica_exit pica_game_begin pica_game_end pica_flush_vertices pica_frame_started pica_frame_ended \
	pica_thread_call pica_thread_sync pica_state_lost pica_prepare_present pica_picture pica_stats pica_probe pica_debug pica_open_frame pica_async pica_double_buffer pica_wait_frame pica_frame_pending pica_dump_vram

core: patch
	$(MAKE) -C core -f Makefile.libretro platform=ctr CTR_GPU=dual EXTRA_EXTERN_SYMS="$(EXTERN_SYMS)"

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
# diagnostics (0.3.1/0.3.2): public 0.3.0 must replace those without downgrading.
# The application/3DSX still reports its actual semantic version (0.3.0).
VERSION    ?= $(shell sed -n 's/.*PSX3D_VERSION *"\([0-9]*\)\.\([0-9]*\)\.\([0-9]*\)".*/\1 \2 \3/p' psx3d/source/psx3d.h | awk '{print ($$1*16 + $$2)*1024 + $$3 + 3}')

cia: app
	$(BANNERTOOL) makebanner -i psx3d/meta/banner.png -a psx3d/meta/banner.wav -o psx3d/build/banner.bnr
	$(BANNERTOOL) makesmdh -s "PSX3D" -l "PSX3D - PS1 emulator for 3DS" -p "BreadChaser" \
		-i psx3d/meta/icon.png -o psx3d/build/icon.icn
	$(MAKEROM) -f cia -o psx3d/PSX3D.cia -elf psx3d/PSX3D.elf -rsf psx3d/meta/app.rsf \
		-icon psx3d/build/icon.icn -banner psx3d/build/banner.bnr -target t -ver $(VERSION)

clean:
	$(MAKE) -C core -f Makefile.libretro platform=ctr CTR_GPU=dual EXTRA_EXTERN_SYMS="$(EXTERN_SYMS)" clean
	$(MAKE) -C psx3d clean
