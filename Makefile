# Builds the PCSX-ReARMed core as a static library, then the PSX3D app.
.PHONY: all patch core app clean

all: app

# small fixes to the upstream core, kept as patches so the submodule stays upstream
patch:
	@for p in $(CURDIR)/patches/*.patch; do \
		git -C core apply --reverse --check $$p 2>/dev/null || git -C core apply $$p; \
	done

core: patch
	$(MAKE) -C core -f Makefile.libretro platform=ctr EXTRA_EXTERN_SYMS="psx3d_depth_get psx3d_depth_frame"

app: core
	$(MAKE) -C psx3d

clean:
	$(MAKE) -C core -f Makefile.libretro platform=ctr EXTRA_EXTERN_SYMS="psx3d_depth_get psx3d_depth_frame" clean
	$(MAKE) -C psx3d clean
