# Builds the PCSX-ReARMed core as a static library, then the PSX3D app.
.PHONY: all core app clean

all: app

core:
	$(MAKE) -C core -f Makefile.libretro platform=ctr

app: core
	$(MAKE) -C psx3d

clean:
	$(MAKE) -C core -f Makefile.libretro platform=ctr clean
	$(MAKE) -C psx3d clean
