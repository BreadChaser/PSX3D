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
		/* Preserve existing per-function cache sets across gameplay palette edits. */
		. = . + ((0x380 - .) & 0xfff);
		emu.o(.text.autosave_failed)
		. = . + ((0x444 - .) & 0xfff);
		emu.o(.text.save_take)
		. = . + ((0x588 - .) & 0xfff);
		emu.o(.text.clip_frame)
		. = . + ((0x6ac - .) & 0xfff);
		emu.o(.text.menu_bar)
		. = . + ((0x734 - .) & 0xfff);
		emu.o(.text.bottom_backlight)
		. = . + ((0x7dc - .) & 0xfff);
		emu.o(.text.state_save_to)
		. = . + ((0x9d0 - .) & 0xfff);
		emu.o(.text.clip_game_path.constprop.0)
		. = . + ((0xa88 - .) & 0xfff);
		emu.o(.text.clip_start)
		. = . + ((0xde4 - .) & 0xfff);
		emu.o(.text.autosave_collect.part.0)
		. = . + ((0xe9c - .) & 0xfff);
		emu.o(.text.menu_draw_bottom)
		. = . + ((0xff0 - .) & 0xfff);
		emu.o(.text.menu_draw_top)
		. = . + ((0x160 - .) & 0xfff);
		emu.o(.text.save_put)
		. = . + ((0x290 - .) & 0xfff);
		emu.o(.text.autosave_writer)
		. = . + ((0x2e0 - .) & 0xfff);
		emu.o(.text.autosave_start)
		. = . + ((0x4c8 - .) & 0xfff);
		emu.o(.text.game_apt)
		. = . + ((0x7bc - .) & 0xfff);
		emu.o(.text.emu_game_title)
		. = . + ((0x7c8 - .) & 0xfff);
		emu.o(.text.emu_record_start)
		. = . + ((0x85c - .) & 0xfff);
		emu.o(.text.emu_osd)
		. = . + ((0x864 - .) & 0xfff);
		emu.o(.text.state_load_from)
		. = . + ((0x8ec - .) & 0xfff);
		emu.o(.text.emu_state_format)
		. = . + ((0x928 - .) & 0xfff);
		emu.o(.text.emu_debug_state)
		. = . + ((0x978 - .) & 0xfff);
		emu.o(.text.emu_init)
		. = . + ((0x9f8 - .) & 0xfff);
		emu.o(.text.emu_deinit)
		. = . + ((0xa34 - .) & 0xfff);
		emu.o(.text.emu_run_game)
		. = . + ((0x6e0 - .) & 0xfff);
		emu.o(.text.disc_count)
		. = . + ((0x70c - .) & 0xfff);
		emu.o(.text.disc_query)
		. = . + ((0x7b0 - .) & 0xfff);
		emu.o(.text.osd_push)
		. = . + ((0x920 - .) & 0xfff);
		emu.o(.text.clip_osd)
		. = . + ((0x9b8 - .) & 0xfff);
		emu.o(.text.write_file)
		. = . + ((0xa50 - .) & 0xfff);
		emu.o(.text.file_id)
		. = . + ((0xa84 - .) & 0xfff);
		emu.o(.text.saved_at)
		. = . + ((0xaac - .) & 0xfff);
		emu.o(.text.ref_entry)
		. = . + ((0xad8 - .) & 0xfff);
		emu.o(.text.sta_retire)
		. = . + ((0xb80 - .) & 0xfff);
		emu.o(.text.disc_map)
		. = . + ((0xbfc - .) & 0xfff);
		emu.o(.text.ref_of)
		. = . + ((0xc9c - .) & 0xfff);
		emu.o(.text.undo_set)
		. = . + ((0xcdc - .) & 0xfff);
		emu.o(.text.menu_build)
		. = . + ((0xf48 - .) & 0xfff);
		emu.o(.text.base_name)
		. = . + ((0xf68 - .) & 0xfff);
		emu.o(.text.disc_names_init)
		. = . + ((0x0bc - .) & 0xfff);
		emu.o(.text.explain_failure)
		. = . + ((0x1a0 - .) & 0xfff);
		emu.o(.text.undo_resolve)
		. = . + ((0x304 - .) & 0xfff);
		emu.o(.text.slot_leftovers)
		. = . + ((0x354 - .) & 0xfff);
		emu.o(.text.safety_copy)
		. = . + ((0x65c - .) & 0xfff);
		emu.o(.text.alert_draw_top)
		. = . + ((0x770 - .) & 0xfff);
		emu.o(.text.slot_paths.constprop.0)
		. = . + ((0x808 - .) & 0xfff);
		emu.o(.text.slot_save_over)
		. = . + ((0x950 - .) & 0xfff);
		emu.o(.text.emu_osd_alert)
		. = . + ((0x958 - .) & 0xfff);
		emu.o(.text.card_trouble_dialog)
		. = . + ((0xa8c - .) & 0xfff);
		emu.o(.text.continue_declined)
		. = . + ((0xce4 - .) & 0xfff);
		emu.o(.text.card_put_back)
		. = . + ((0xd60 - .) & 0xfff);
		emu.o(.text.state_load_file)
		. = . + ((0x040 - .) & 0xfff);
		emu.o(.text.state_load)
		. = . + ((0x0bc - .) & 0xfff);
		emu.o(.text.ring_restore)
		. = . + ((0x130 - .) & 0xfff);
		emu.o(.text.continue_prompt)
		. = . + ((0x240 - .) & 0xfff);
		emu.o(.text.undo_run.isra.0)
		. = . + ((0x5f0 - .) & 0xfff);
		emu.o(.text.emu_load_error)
		emu.o(.text .text.*)
		. = . + ((0x3bc - .) & 0xfff);
		__psx3d_frontend_hot_gpu = .;
		gamecfg.o(.text .text.*)
		gpu_cache.o(.text .text.*)
		/* gpustereo.o: the functions the Old 3DS runs every frame (gpustereo_mode
		   to gpustereo_status) at RC9's page offsets; the rest (Real 3D's
		   mirrored draws and copies, New 3DS only) after them. */
		gpustereo.o(.text)
		. = . + ((0x318 - .) & 0xfff);
		gpustereo.o(.text.gpustereo_mode .text.__wrap_psx3d_depth_cmd .text.__wrap_psx3d_stereo_set .text.gpustereo_frame .text.gpustereo_after_present .text.gpustereo_game_end .text.__wrap_C3D_DrawArrays .text.__wrap_GX_TextureCopy .text.gpustereo_frame_end .text.gpustereo_ready .text.gpustereo_flat .text.gpustereo_right_for .text.gpustereo_status)
		gpustereo.o(.text.*)
		. = . + ((0xa58 - .) & 0xfff);
		/* Real 3D (New 3DS only) between gpustereo.o and input.o: the
		   functions the Old 3DS runs every frame (gs_core_frame to
		   gs_core_pass_shown) from RC9's address of gs_core_frame; the
		   capture and the shift come with the rest of the frontend. */
		gpustereo_core.o(.text .text.vput .text.vget .text.unlikely.gs_core_alloc .text.unlikely.gs_core_free .text.unlikely.gs_core_clear .text.put_tri .text.gs_capture .text.gs_drain .text.unlikely.gs_core_queue)
		. = . + ((0x114 - .) & 0xfff);
		gpustereo_core.o(.text.unlikely.gs_core_frame)
		. = . + ((0x4d4 - .) & 0xfff);
		gpustereo_core.o(.text.gs_focus .text.gs_flat .text.unlikely.gs_core_counts .text.unlikely.gs_core_pass_shown)
		. = . + ((0xc8 - .) & 0xfff);
		/* Preserve existing per-function cache sets across gameplay palette edits. */
		. = . + ((0x0c8 - .) & 0xfff);
		input.o(.text.stats_contains)
		. = . + ((0x168 - .) & 0xfff);
		input.o(.text.control_item)
		. = . + ((0x298 - .) & 0xfff);
		input.o(.text.button_ink.part.0)
		. = . + ((0x334 - .) & 0xfff);
		input.o(.text.touch_map_enabled)
		. = . + ((0x3a8 - .) & 0xfff);
		input.o(.text.input_hotkey_combo_name.part.0)
		. = . + ((0x438 - .) & 0xfff);
		input.o(.text.input_stick_binding.part.0)
		. = . + ((0x490 - .) & 0xfff);
		input.o(.text.touch_mark.part.0)
		. = . + ((0x51c - .) & 0xfff);
		input.o(.text.touch_panel_draw.isra.0)
		. = . + ((0x664 - .) & 0xfff);
		input.o(.text.input_ps1_name)
		. = . + ((0x684 - .) & 0xfff);
		input.o(.text.input_map_factory)
		. = . + ((0x6a0 - .) & 0xfff);
		input.o(.text.input_map_key_name)
		. = . + ((0x6c0 - .) & 0xfff);
		input.o(.text.input_map_raw)
		. = . + ((0x754 - .) & 0xfff);
		input.o(.text.input_map_get)
		. = . + ((0x8c8 - .) & 0xfff);
		input.o(.text.touch_parts)
		. = . + ((0xa34 - .) & 0xfff);
		input.o(.text.touch_button)
		. = . + ((0xdf8 - .) & 0xfff);
		input.o(.text.input_map_set)
		. = . + ((0xeec - .) & 0xfff);
		input.o(.text.input_map_cycle)
		. = . + ((0xf88 - .) & 0xfff);
		input.o(.text.input_map_reset)
		. = . + ((0x008 - .) & 0xfff);
		input.o(.text.input_hotkey_factory)
		. = . + ((0x018 - .) & 0xfff);
		input.o(.text.input_hotkey_get)
		. = . + ((0x034 - .) & 0xfff);
		input.o(.text.input_hotkey_set)
		. = . + ((0x050 - .) & 0xfff);
		input.o(.text.input_hotkey_key_name)
		. = . + ((0x070 - .) & 0xfff);
		input.o(.text.input_hotkey_combo_name)
		. = . + ((0x08c - .) & 0xfff);
		input.o(.text.input_hotkey_load)
		. = . + ((0x114 - .) & 0xfff);
		input.o(.text.input_gameplay)
		. = . + ((0x144 - .) & 0xfff);
		input.o(.text.input_remote_key)
		. = . + ((0x1c0 - .) & 0xfff);
		input.o(.text.input_poll)
		. = . + ((0x970 - .) & 0xfff);
		input.o(.text.input_controller_apply)
		. = . + ((0x9c4 - .) & 0xfff);
		input.o(.text.input_keys_down)
		. = . + ((0x9d4 - .) & 0xfff);
		input.o(.text.input_keys_held)
		. = . + ((0x9e4 - .) & 0xfff);
		input.o(.text.input_fast_forward)
		. = . + ((0xa40 - .) & 0xfff);
		input.o(.text.touch_button_lit)
		. = . + ((0xa8c - .) & 0xfff);
		input.o(.text.touch_rect_draw)
		. = . + ((0xc68 - .) & 0xfff);
		input.o(.text.input_fast_forward_off)
		. = . + ((0xc80 - .) & 0xfff);
		input.o(.text.input_rewind)
		. = . + ((0xcb8 - .) & 0xfff);
		input.o(.text.input_state)
		. = . + ((0xfc0 - .) & 0xfff);
		input.o(.text.touch_buttons_active)
		. = . + ((0xfd4 - .) & 0xfff);
		input.o(.text.input_touch_begin)
		. = . + ((0x130 - .) & 0xfff);
		input.o(.text.input_touch_disc_log)
		. = . + ((0x200 - .) & 0xfff);
		input.o(.text.input_touch_reset)
		. = . + ((0x248 - .) & 0xfff);
		input.o(.text.input_stats_touched)
		. = . + ((0x260 - .) & 0xfff);
		input.o(.text.menu_touched)
		. = . + ((0x278 - .) & 0xfff);
		input.o(.text.input_touch_changed)
		. = . + ((0x3a8 - .) & 0xfff);
		input.o(.text.input_controls_count)
		. = . + ((0x3d0 - .) & 0xfff);
		input.o(.text.input_control_slot)
		. = . + ((0x3e4 - .) & 0xfff);
		input.o(.text.input_controls_help)
		. = . + ((0x444 - .) & 0xfff);
		input.o(.text.input_control_label)
		. = . + ((0x4f0 - .) & 0xfff);
		input.o(.text.input_control_value)
		. = . + ((0x680 - .) & 0xfff);
		input.o(.text.input_control_color)
		. = . + ((0x704 - .) & 0xfff);
		input.o(.text.input_control_cycle)
		. = . + ((0x8b4 - .) & 0xfff);
		input.o(.text.touch_buttons_draw)
		. = . + ((0x460 - .) & 0xfff);
		input.o(.text.input_control_key)
		. = . + ((0x4e0 - .) & 0xfff);
		input.o(.text.config_control_overridden)
		input.o(.text .text.*)
		. = . + ((0x210 - .) & 0xfff);
		__psx3d_frontend_hot_queue = .;
		renderqueue.o(.text .text.*)
		. = . + ((0x24c - .) & 0xfff);
		__psx3d_frontend_hot_stats = .;
		/* Preserve existing per-function cache sets across gameplay palette edits. */
		. = . + ((0x24c - .) & 0xfff);
		statsview.o(.text.parse)
		. = . + ((0x2ec - .) & 0xfff);
		statsview.o(.text.statsview_collecting)
		. = . + ((0x35c - .) & 0xfff);
		statsview.o(.text.statsview_begin)
		. = . + ((0x430 - .) & 0xfff);
		statsview.o(.text.statsview_frame)
		. = . + ((0x4b8 - .) & 0xfff);
		statsview.o(.text.statsview_emulated)
		. = . + ((0x4c8 - .) & 0xfff);
		statsview.o(.text.statsview_update)
		. = . + ((0xbc0 - .) & 0xfff);
		statsview.o(.text.statsview_draw)
		. = . + ((0xeb0 - .) & 0xfff);
		statsview.o(.text.statsview_cycle)
		. = . + ((0xf94 - .) & 0xfff);
		statsview.o(.text.statsview_touch)
		. = . + ((0xfbc - .) & 0xfff);
		statsview.o(.text.statsview_redraw)
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
		/* Shared UI primitives are also called by gameplay touch controls/FPS.
		   Keep their rc9 cache sets when menu code grows or shrinks. */
		. = . + ((0xfc0 - .) & 0xfff);
		ui.o(.text.symbol_line)
		. = . + ((0x180 - .) & 0xfff);
		ui.o(.text.ui_begin)
		. = . + ((0x190 - .) & 0xfff);
		ui.o(.text.ui_text)
		. = . + ((0x21c - .) & 0xfff);
		ui.o(.text.ui_fit)
		. = . + ((0x3f0 - .) & 0xfff);
		ui.o(.text.ui_rect)
		. = . + ((0x410 - .) & 0xfff);
		ui.o(.text.ui_text_ex)
		. = . + ((0x494 - .) & 0xfff);
		ui.o(.text.ui_text_wrap)
		. = . + ((0x6b8 - .) & 0xfff);
		ui.o(.text.ui_text_pixels)
		. = . + ((0xd38 - .) & 0xfff);
		ui.o(.text.ui_input_color)
		. = . + ((0xd70 - .) & 0xfff);
		ui.o(.text.ui_input_symbol)
		. = . + ((0xdbc - .) & 0xfff);
		libui.o(.text.libui_font)
		__psx3d_frontend_hot_end = .;
		/* Menu diagnostics live outside the protected per-frame block. */
		*(.menu_text)
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
	.theme_game_bss ALIGN(0x1000) (NOLOAD) : { *(.theme_game.bss) } : data
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
