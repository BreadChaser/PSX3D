/* PSX3D link layout: emulator core first, then libraries, then the frontend.
 *
 * Old 3DS speed depends on code and data layout: its 16 KB 4-way caches have
 * no L2, and the cache set of an address is its offset within a 4 KiB page
 * (address mod 0x1000). Before this script the frontend linked in front of
 * the core, so frontend growth moved every core function.
 *
 * Here every output section starts with the core (pcsx_rearmed_libretro_ctr.a:
 * dynarec and its assembly, GTE, PS1 memory/hardware, gpu_pica + gpulib,
 * dfsound, libretro glue), followed by the libraries, then the frontend. The
 * frontend can grow or shrink without moving anything before it:
 *   - .text: core and library code have fixed addresses.
 *   - .rodata/.data/.bss: start on a page after the previous segment, so
 *     frontend code growth moves them by whole pages only; the core's offsets
 *     within a page (its cache sets) and its internal layout never change.
 * tools/layout/check.sh proves this with a deliberately grown frontend.
 *
 * Each block starts at a fixed offset within a 4 KiB page,
 *   . = . + ((OFFSET - .) & 0xfff);
 * a pad of at most 4095 bytes (inside an output section "." counts from the
 * section's start, so every section that pins starts on a page; the ASSERTs
 * at the end check the result). The offsets are the ones v0.4.1 RC4 had, so
 * core and libraries keep RC4's cache sets (hardware A/B/A in
 * docs/dev/core-first.md). The frontend's start is pinned the same way.
 *
 * Core changes still move the core's own later code: NDRC_LAYOUT_PAD in
 * new_dynarec.c pins what follows the dynarec (see there).
 *
 * Based on devkitARM's 3dsx.ld (Mozilla Public License 2.0). */
OUTPUT_FORMAT("elf32-littlearm", "elf32-bigarm", "elf32-littlearm")
OUTPUT_ARCH(arm)
ENTRY(_start)

PHDRS
{
	code   PT_LOAD FLAGS(5) /* Read | Execute */;
	rodata PT_LOAD FLAGS(4) /* Read */;
	data   PT_LOAD FLAGS(6) /* Read | Write */;
}

SECTIONS
{
	/* =========== CODE section =========== */

	PROVIDE(__start__ = 0x100000);
	. = __start__;

	.text ALIGN(0x1000) :
	{
		KEEP( *(.crt0) )
		KEEP( *(.init) )
		. = ALIGN(4);

		/* Core assembly (dynarec linkage, GTE, SPU, memcpy). */
		. = . + ((0x0b0 - .) & 0xfff);
		__psx3d_core_text_start = .;
		*pcsx_rearmed_libretro_ctr.a:(.text)
		__psx3d_core_asm_end = .;
		/* Core C code. psx3d_core_align (its page) is the name older
		   layout tools (codecmp.py) know. */
		. = . + ((0x4f0 - .) & 0xfff);
		psx3d_core_align = . - 0x4f0;
		*pcsx_rearmed_libretro_ctr.a:(.text.*)
		__psx3d_core_text_end = .;

		/* Libraries: compiler/libc assembly, then the rest in link order. */
		. = . + ((0x988 - .) & 0xfff);
		__psx3d_libs_text_start = .;
		*.a:(.text)
		*crtbegin.o(.text .text.*)
		. = . + ((0xd08 - .) & 0xfff);
		*.a:(.text.*)
		__psx3d_libs_text_end = .;

		/* Frontend: may grow freely. */
		. = . + ((0xae8 - .) & 0xfff);
		__psx3d_frontend_text_start = .;
		/* The frontend's per-frame code (sound, the game loop, GPU cache
		   wraps, 3D, input, render queue, FPS counter, picture output) comes
		   first, in RC4's order and at the page offsets (cache sets) it had in
		   v0.5.0 RC4. Growth anywhere else in the frontend (menus, saves)
		   then moves none of it; growth inside a block moves only the rest
		   of that block. Objects that grew since RC4 (audio, gpustereo,
		   video) have the object after them pinned to its RC4 page offset
		   too. */
		audio.o(.text .text.*)
		audio_resample.o(.text .text.*)
		. = . + ((0x628 - .) & 0xfff);
		audio_tsm.o(.text .text.*)
		. = . + ((0x120 - .) & 0xfff);
		__psx3d_frontend_hot_emu = .;
		/* emu.o: its per-frame functions (the core's callbacks, the frame
		   pacer's observer, the in-game screens, run_game) at their RC4 page
		   offsets one by one; the rest of emu.o follows them. A name that
		   no longer exists just lands with the rest. */
		emu.o(.text.menu_label .text.menu_value .text.osd .text.state_head_ok)
		. = . + ((0x20c - .) & 0xfff);
		emu.o(.text.input_state_cb .text.input_poll_cb .text.video_cb .text.environment .text.core_log)
		. = . + ((0xddc - .) & 0xfff);
		emu.o(.text.audio_batch_cb .text.audio_sample_cb)
		. = . + ((0x610 - .) & 0xfff);
		emu.o(.text.psx3d_smart_observe.constprop.0)
		. = . + ((0x6d4 - .) & 0xfff);
		emu.o(.text.game_draw_top)
		. = . + ((0x898 - .) & 0xfff);
		emu.o(.text.game_draw_bottom)
		. = . + ((0x780 - .) & 0xfff);
		emu.o(.text.run_game)
		. = . + ((0x218 - .) & 0xfff);
		emu.o(.text.emu_audio_ticks .text.emu_frontend_stats)
		. = . + ((0x360 - .) & 0xfff);
		emu.o(.text.__wrap_fread)
		emu.o(.text .text.*)
		. = . + ((0x3bc - .) & 0xfff);
		__psx3d_frontend_hot_gpu = .;
		gamecfg.o(.text .text.*)
		gpu_cache.o(.text .text.*)
		gpustereo.o(.text .text.*)
		. = . + ((0xa58 - .) & 0xfff);
		gpustereo_core.o(.text .text.*)
		. = . + ((0xc8 - .) & 0xfff);
		input.o(.text .text.*)
		. = . + ((0x210 - .) & 0xfff);
		__psx3d_frontend_hot_queue = .;
		renderqueue.o(.text .text.*)
		. = . + ((0x24c - .) & 0xfff);
		__psx3d_frontend_hot_stats = .;
		statsview.o(.text .text.*)
		. = . + ((0xb90 - .) & 0xfff);
		__psx3d_frontend_hot_video = .;
		/* video.o up to video_present_screens, then the rest from the page
		   offset rc1 had it at (the 3D screen-depth rounding made
		   video_present_screens longer). */
		video.o(.text .text.screen_box .text.video_gpu_frame_begin.part.0 .text.video_gpu_frame_begin .text.draw_picture.isra.0 .text.draw_game .text.top_begin .text.video_stereo_targets.part.0 .text.video_take_gpu_stats .text.video_take_wait_ticks .text.video_stereo_targets .text.video_stereo_on .text.video_top_aa .text.video_init .text.renderer_is_gpu .text.video_exit .text.video_bottom_target .text.video_framebuffer .text.video_frame .text.video_probe_top .text.video_take_xfer_ticks .text.video_gpu_idle .text.video_game_begin .text.idle_on_gpu_thread .text.video_present_screens)
		. = . + ((0x308 - .) & 0xfff);
		video.o(.text .text.*)
		. = . + ((0xa98 - .) & 0xfff);
		widescreen.o(.text .text.*)
		picadirect.o(.text .text.*)
		/* Per-frame objects newer than RC4: after RC4's block, so they move
		   none of it. */
		uploadskip.o(.text .text.*)
		display.o(.text .text.*)
		__psx3d_frontend_hot_end = .;
		/* The rest of the frontend, in link order. */
		*(.text)
		*(.text.*)
		*(.glue_7)
		*(.glue_7t)
		*(.stub)
		*(.gnu.warning)
		*(.gnu.linkonce.t*)
		/* Optional renderer feedback path: its growth must not move the
		   existing core or library code. This is live code, not padding.
		   Native sprites run per draw in render-to-texture games: it
		   starts at the page offset v0.5.1 rc1 was measured with on the Old
		   3DS, so frontend growth before it leaves its cache sets alone. */
		. = . + ((0xea8 - .) & 0xfff);
		*(.pica_rtt.text)
		/* Save/menu/message code marked PSX3D_COLD (psx3d.h). */
		*(.psx3d_cold.text)
		. = ALIGN(4);
		__psx3d_frontend_text_end = .;

		/* .fini */
		KEEP( *(.fini) )
		. = ALIGN(4);
	} : code

	/* =========== RODATA section =========== */

	. = ALIGN(0x1000);

	/* Core constants, each followed by the libraries' (as in RC4). Strings
	   (mergeable sections) stay out: the linker merges them per output
	   section, and a shared pool would let frontend strings move the core's.
	   A library gaining constants (the frontend calls a new library
	   function) moves later core constants by whole pages at most. */
	.rodata :
	{
		. = . + ((0xdf8 - .) & 0xfff);
		__psx3d_core_rodata_start = .;
		INPUT_SECTION_FLAGS(!SHF_MERGE) *pcsx_rearmed_libretro_ctr.a:(.rodata)
		__psx3d_core_rodata_plain_end = .;
		INPUT_SECTION_FLAGS(!SHF_MERGE) *.a:(.rodata)
		. = . + ((0x45c - .) & 0xfff);
		__psx3d_core_tables_start = .;
		INPUT_SECTION_FLAGS(!SHF_MERGE) *pcsx_rearmed_libretro_ctr.a:(.rodata.*)
		__psx3d_core_tables_end = .;
		INPUT_SECTION_FLAGS(!SHF_MERGE) *.a:(.rodata.*)
		. = . + ((0x400 - .) & 0xfff);
	} : rodata

	/* The core's strings, merged among themselves. */
	.rodata.core_strings :
	{
		__psx3d_core_strings_start = .;
		*pcsx_rearmed_libretro_ctr.a:(.rodata .rodata.*)
		__psx3d_core_rodata_end = .;
	} : rodata

	/* The frontend's constants and every other string, from a page start
	   as in RC4. */
	.rodata.frontend ALIGN(0x1000) :
	{
		__psx3d_frontend_rodata_start = .;
		*(.rodata)
		*(.roda)
		*(.rodata.*)
		*all.rodata*(*)
		*(.gnu.linkonce.r*)
		SORT(CONSTRUCTORS)
		. = ALIGN(4);
		__tdata_align = .;
		LONG (ALIGNOF(.tdata));
		. = ALIGN(4);
	} : rodata

	.ARM.extab : { *(.ARM.extab* .gnu.linkonce.armextab.*) } : rodata
	__exidx_start = .;
	.ARM.exidx : { *(.ARM.exidx* .gnu.linkonce.armexidx.*) } : rodata
	__exidx_end = .;

	/* =========== DATA section =========== */

	. = ALIGN(0x1000);

	.data :
	{
		. = . + ((0x1c0 - .) & 0xfff);
		__psx3d_core_data_start = .;
		*pcsx_rearmed_libretro_ctr.a:(.data .data.*)
		__psx3d_core_data_end = .;
		*.a:(.data .data.*)
		. = . + ((0x000 - .) & 0xfff);
		__psx3d_frontend_data_start = .;
		*(.data)
		*(.data.*)
		*(.gnu.linkonce.d*)
		CONSTRUCTORS
		. = ALIGN(4);
	} : data

	.tdata : ALIGN(4)
	{
		__tdata_lma = .;
		*(.tdata)
		*(.tdata.*)
		*(.gnu.linkonce.td.*)
		. = ALIGN(4);
		__tdata_lma_end = .;
	} : data

	.tbss : ALIGN(4)
	{
		*(.tbss)
		*(.tbss.*)
		*(.gnu.linkonce.tb.*)
		*(.tcommon)
		. = ALIGN(4);
	} : data

	.preinit_array ALIGN(4) :
	{
		PROVIDE (__preinit_array_start = .);
		KEEP (*(.preinit_array))
		PROVIDE (__preinit_array_end = .);
	} : data

	.init_array ALIGN(4) :
	{
		PROVIDE (__init_array_start = .);
		KEEP (*(SORT(.init_array.*)))
		KEEP (*(.init_array))
		PROVIDE (__init_array_end = .);
	} : data

	.fini_array ALIGN(4) :
	{
		PROVIDE (__fini_array_start = .);
		KEEP (*(.fini_array))
		KEEP (*(SORT(.fini_array.*)))
		PROVIDE (__fini_array_end = .);
	} : data

	.ctors ALIGN(4) :
	{
		KEEP (*crtbegin.o(.ctors)) /* MUST be first -- GCC requires it */
		KEEP (*(EXCLUDE_FILE (*crtend.o) .ctors))
		KEEP (*(SORT(.ctors.*)))
		KEEP (*(.ctors))
	} : data

	.dtors ALIGN(4) :
	{
		KEEP (*crtbegin.o(.dtors))
		KEEP (*(EXCLUDE_FILE (*crtend.o) .dtors))
		KEEP (*(SORT(.dtors.*)))
		KEEP (*(.dtors))
	} : data

	__bss_start__ = .;
	.bss ALIGN(0x1000) :
	{
		/* Core first, as above: dynarec_local and psxRegs lead. The small
		   plain .bss of libraries sits between the core's two blocks as in
		   RC4; its growth moves the second block by whole pages at most. */
		. = . + ((0x4a0 - .) & 0xfff);
		__psx3d_core_bss_start = .;
		*pcsx_rearmed_libretro_ctr.a:(.bss)
		__psx3d_core_bss_plain_end = .;
		*.a:(.bss)
		*crtbegin.o(.bss .bss.*)
		. = . + ((0x058 - .) & 0xfff);
		__psx3d_core_bss2_start = .;
		*pcsx_rearmed_libretro_ctr.a:(.bss.*)
		__psx3d_core_bss_end = .;
		*.a:(.bss.*)
		. = . + ((0x658 - .) & 0xfff);
		__psx3d_frontend_bss_start = .;
		/* The per-frame frontend's variables, at their RC4 page offsets
		   (as for its code above). */
		audio.o(.bss .bss.*)
		audio_resample.o(.bss .bss.*)
		. = . + ((0xa18 - .) & 0xfff);
		emu.o(.bss .bss.*)
		. = . + ((0xb2c - .) & 0xfff);
		gamecfg.o(.bss .bss.*)
		gpu_cache.o(.bss .bss.*)
		gpustereo.o(.bss .bss.*)
		gpustereo_core.o(.bss .bss.*)
		input.o(.bss .bss.*)
		. = . + ((0x9f8 - .) & 0xfff);
		renderqueue.o(.bss .bss.*)
		. = . + ((0x490 - .) & 0xfff);
		statsview.o(.bss .bss.*)
		. = . + ((0xbf0 - .) & 0xfff);
		video.o(.bss .bss.*)
		widescreen.o(.bss .bss.*)
		uploadskip.o(.bss .bss.*)
		display.o(.bss .bss.*)
		/* The rest of the frontend's variables, in link order. */
		*(.dynbss)
		*(.bss)
		*(.bss.*)
		*(.gnu.linkonce.b*)
		*(COMMON)

		/* Reserve space for the TLS segment of the main thread.
		   We need (__tls_start - 8) to be aligned the same as .tdata, to account for
		   the 8-byte ARM TLS header. Since the header is not actually used for
		   ARM_TLS_LE32 relocation, we just fake it by subtracting 8 from the data
		   offset.
		 */
		. = 8 + ABSOLUTE(ALIGN(ABSOLUTE(. - 8), MAX(ALIGNOF(.tdata), ALIGNOF(.tbss))));
		__tls_start = .;
		. += SIZEOF(.tdata);

        /* Ensure the alignment of .tbss is accounted for in the total size,
           since SIZEOF(.tdata) doesn't include any padding before .tbss.
         */
        . = ALIGN(ALIGNOF(.tbss));
        . += SIZEOF(.tbss);
		__tls_end = .;
	} : data
	/* RTT descriptors are deliberately outside the established core BSS.
	   Keep them in startup's zero range without turning .bss into PROGBITS. */
	.pica_rtt_bss (NOLOAD) : { *(.pica_rtt.bss) } : data
	__bss_end__ = .;

	__end__ = ABSOLUTE(.) ;

	ASSERT((__psx3d_core_text_start == 0x1000b0) && (psx3d_core_align == 0x103000),
		"core-first.ld: the core's code must start at 0x1000b0 / 0x1034f0")
	ASSERT(((__psx3d_core_rodata_start & 0xfff) == 0xdf8) && ((__psx3d_core_data_start & 0xfff) == 0x1c0)
		&& ((__psx3d_core_bss_start & 0xfff) == 0x4a0), "core-first.ld: core data pins")

	/* ==================
	   ==== Metadata ====
	   ================== */

	/* Discard sections that difficult post-processing */
	/DISCARD/ : { *(.group .comment .note) }

	/* Stabs debugging sections. */
	.stab          0 : { *(.stab) }
	.stabstr       0 : { *(.stabstr) }
	.stab.excl     0 : { *(.stab.excl) }
	.stab.exclstr  0 : { *(.stab.exclstr) }
	.stab.index    0 : { *(.stab.index) }
	.stab.indexstr 0 : { *(.stab.indexstr) }

	/* DWARF 1 */
	.debug          0 : { *(.debug) }
	.line           0 : { *(.line) }

	/* GNU DWARF 1 extensions */
	.debug_srcinfo  0 : { *(.debug_srcinfo) }
	.debug_sfnames  0 : { *(.debug_sfnames) }

	/* DWARF 1.1 and DWARF 2 */
	.debug_aranges  0 : { *(.debug_aranges) }
	.debug_pubnames 0 : { *(.debug_pubnames) }

	/* DWARF 2 */
	.debug_info     0 : { *(.debug_info) }
	.debug_abbrev   0 : { *(.debug_abbrev) }
	.debug_line     0 : { *(.debug_line) }
	.debug_frame    0 : { *(.debug_frame) }
	.debug_str      0 : { *(.debug_str) }
	.debug_loc      0 : { *(.debug_loc) }
	.debug_macinfo  0 : { *(.debug_macinfo) }
}
