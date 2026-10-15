# Builds the PCSX-ReARMed core as a static library, then the PSX3D app.
.PHONY: all patch core app cia clean

all: app

# small fixes to the upstream core, kept as patches so the submodule stays upstream
patch:
	@for p in $(CURDIR)/patches/*.patch; do \
		git -C core apply --reverse --check $$p 2>/dev/null || git -C core apply $$p; \
	done

core: patch
	$(MAKE) -C core -f Makefile.libretro platform=ctr EXTRA_EXTERN_SYMS="psx3d_depth_get psx3d_depth_frame psx3d_overlay_get psx3d_ov_seq"

app: core
	$(MAKE) -C psx3d

# Installable CIA. Needs makerom (Project_CTR) and bannertool on PATH,
# or MAKEROM=/path/to/makerom BANNERTOOL=/path/to/bannertool.
MAKEROM    ?= makerom
BANNERTOOL ?= bannertool
VERSION    ?= 0

cia: app
	$(BANNERTOOL) makebanner -i psx3d/meta/banner.png -a psx3d/meta/banner.wav -o psx3d/build/banner.bnr
	$(BANNERTOOL) makesmdh -s "PSX3D" -l "PSX3D - PS1 emulator (PCSX-ReARMed)" -p "PSX3D" \
		-i psx3d/meta/icon.png -o psx3d/build/icon.icn
	$(MAKEROM) -f cia -o psx3d/PSX3D.cia -elf psx3d/PSX3D.elf -rsf psx3d/meta/app.rsf \
		-icon psx3d/build/icon.icn -banner psx3d/build/banner.bnr -target t -ver $(VERSION)

clean:
	$(MAKE) -C core -f Makefile.libretro platform=ctr EXTRA_EXTERN_SYMS="psx3d_depth_get psx3d_depth_frame psx3d_overlay_get psx3d_ov_seq" clean
	$(MAKE) -C psx3d clean
