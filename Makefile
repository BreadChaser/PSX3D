# Builds the PCSX-ReARMed core as a static library, then the PSX3D app.
.PHONY: all patch core app cia clean

all: app

# small fixes to the upstream core, kept as patches so the submodule stays upstream
patch:
	@for p in $(CURDIR)/patches/*.patch; do \
		git -C core apply --reverse --check $$p 2>/dev/null || git -C core apply $$p; \
	done

# Renderer: pica draws on the 3DS GPU (experimental), unai is the CPU renderer.
# Switching needs `make clean` first.
GPU ?= pica
EXTERN_SYMS := psx3d_depth_get psx3d_depth_frame psx3d_overlay_get psx3d_ov_seq psx3d_set_strength \
	psx3d_right_get psx3d_stats psx3d_set_half3d psx3d_prof_t psx3d_prof_c psx3d_prof_on new_dynarec_print_stats
ifeq ($(GPU),pica)
EXTERN_SYMS += pica_init pica_exit pica_game_begin pica_game_end pica_frame_started pica_frame_ended \
	pica_state_lost pica_picture pica_stats pica_probe pica_debug pica_open_frame pica_dump_vram
APP_DEFS := -DPSX3D_PICA
endif

core: patch
	$(MAKE) -C core -f Makefile.libretro platform=ctr CTR_GPU=$(GPU) EXTRA_EXTERN_SYMS="$(EXTERN_SYMS)"

app: core
	$(MAKE) -C psx3d EXTRA_DEFS="$(APP_DEFS)"

# Installable CIA. Needs makerom (Project_CTR) and bannertool on PATH,
# or MAKEROM=/path/to/makerom BANNERTOOL=/path/to/bannertool.
MAKEROM    ?= makerom
BANNERTOOL ?= bannertool
# CIA title version from PSX3D_VERSION. The HOME Menu only refreshes a
# cached icon when this number changes. makerom drops the minor field
# (bits 4-9) of the title version, so major*16+minor goes in the major field
# and patch in the micro field: 0.1.0 -> 1024 (FBI shows it as 1.0.0).
VERSION    ?= $(shell sed -n 's/.*PSX3D_VERSION *"\([0-9]*\)\.\([0-9]*\)\.\([0-9]*\)".*/\1 \2 \3/p' psx3d/source/psx3d.h | awk '{print ($$1*16 + $$2)*1024 + $$3}')

cia: app
	$(BANNERTOOL) makebanner -i psx3d/meta/banner.png -a psx3d/meta/banner.wav -o psx3d/build/banner.bnr
	$(BANNERTOOL) makesmdh -s "PSX3D" -l "PSX3D - PS1 emulator (PCSX-ReARMed)" -p "PSX3D" \
		-i psx3d/meta/icon.png -o psx3d/build/icon.icn
	$(MAKEROM) -f cia -o psx3d/PSX3D.cia -elf psx3d/PSX3D.elf -rsf psx3d/meta/app.rsf \
		-icon psx3d/build/icon.icn -banner psx3d/build/banner.bnr -target t -ver $(VERSION)

clean:
	$(MAKE) -C core -f Makefile.libretro platform=ctr CTR_GPU=$(GPU) EXTRA_EXTERN_SYMS="$(EXTERN_SYMS)" clean
	$(MAKE) -C psx3d clean
