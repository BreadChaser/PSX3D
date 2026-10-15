# Builds the PCSX-ReARMed core as a static library, then the PSX3D app.
.PHONY: all patch core app clean

all: app

# small fixes to the upstream core, kept as patches so the submodule stays upstream
patch:
	@for p in $(CURDIR)/patches/*.patch; do \
		git -C core apply --reverse --check $$p 2>/dev/null || git -C core apply $$p; \
	done

core: patch
	$(MAKE) -C core -f Makefile.libretro platform=ctr

app: core
	$(MAKE) -C psx3d

clean:
	$(MAKE) -C core -f Makefile.libretro platform=ctr clean
	$(MAKE) -C psx3d clean
