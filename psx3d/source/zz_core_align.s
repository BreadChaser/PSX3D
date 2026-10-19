@ Old 3DS speed depends on code layout: its 16 KB instruction cache has no L2,
@ so hot functions that share cache sets evict each other. The core's C code
@ is linked after the frontend, while the dynarec's hot assembly sits at a
@ fixed address before it. Any change in frontend code size used to slide the
@ whole core against that assembly (v0.4.0 final: Crash -5% from 2.3 KB more
@ frontend code). This padding, the last frontend object (.s files link after
@ the .c files), starts the core at a fixed offset modulo 4 KB (one cache way),
@ so frontend changes no longer move it. 1264 is the offset of the measured
@ v0.4.0 rc1 build. Never executed; costs at most 4 KB of .text.
	.section .text.zz_core_align, "axR", %progbits
	.p2align 12
	.global psx3d_core_align
psx3d_core_align:
	.space 1264
