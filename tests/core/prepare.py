#!/usr/bin/env python3
"""Copy the production core to an isolated ARM Linux simulator build (no private files)."""
from pathlib import Path
import shutil
import sys
repo = Path(__file__).resolve().parents[2]
dst = Path(sys.argv[1]).resolve()
shutil.copytree(repo / 'core', dst / 'core', dirs_exist_ok=True, ignore=shutil.ignore_patterns('*.o', '*.d', '*.a', '.git'))
f = dst / 'core/Makefile.libretro'
s = f.read_text()
marker = 'else ifeq ($(platform), ctr)'
parts = s.split(marker)
assert len(parts) == 3
compiler = '''else ifeq ($(platform), ctrsim)
  CC = arm-linux-gnueabihf-gcc
  CXX = arm-linux-gnueabihf-g++
  AR = arm-linux-gnueabihf-ar
  LD = arm-linux-gnueabihf-ld
  OBJCOPY = arm-linux-gnueabihf-objcopy
'''
platform = '''else ifeq ($(platform), ctrsim)
  TARGET := $(TARGET_NAME)_libretro_ctrsim.a
  CFLAGS += -DTARGET_SIZE_2=23 -DARM11 -march=armv6k -mtune=mpcore -mfloat-abi=hard -marm -mfpu=vfp
  CFLAGS += -fomit-frame-pointer -Wno-format -DHAVE_UNISTD_H -DPSX3D_SIM
  OBJS += deps/arm-mem/memcpymove-v6l.o deps/arm-mem/memset-v6l.o
  BUILTIN_GPU = pica
  USE_ASYNC_GPU = 0
  DYNAREC = ari64
  ARCH = arm
  STATIC_LINKING = 1
  HAVE_PHYSICAL_CDROM = 0
  fpic = -fPIC
'''
f.write_text(parts[0] + compiler + marker + parts[1] + platform + marker + parts[2])
f = dst / 'core/libpcsxcore/psxmem.c'
s = f.read_text().replace('ptr = psxMap(0x80000000, 0x00210000, 1, MAP_TAG_RAM);', 'ptr = MAP_FAILED; /* Exercise 3DS relocated RAM. */')
s = s.replace('psxMap(0x77000000,', 'psxMap(0x13000000,').replace('psxMap(0x1f800000, 0x10000,', 'psxMap(0x13400000, 0x10000,')
f.write_text(s)
# Minimal PS-X EXE, entirely synthetic: jump to self, nop. No ROM or BIOS data.
import struct
header = bytearray(2048)
header[:8] = b'PS-X EXE'
struct.pack_into('<IIII', header, 16, 0x80010000, 0, 0x80010000, 2048)
struct.pack_into('<I', header, 48, 0x801fff00)
(dst / 'synthetic.exe').write_bytes(header + struct.pack('<IIII', 0x25080001, 0xac080100, 0x08004000, 0) + bytes(2032))
