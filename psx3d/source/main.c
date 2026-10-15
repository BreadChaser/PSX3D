// PSX3D entry point.
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include "psx3d.h"

// Main thread stack; the core warns below 1MB (dynarec recursion).
u32 __stacksize__ = 0x100000;

// Replaces libctru's heap setup (same steps, libctru 2.6). The default hands
// nearly all free memory to the heaps; we size them from what this console
// actually gives us (~124MB on New 3DS, far less on Old 3DS) and leave a
// reserve so the core can map PS1 RAM/BIOS/LUTs at fixed addresses, which
// makes the dynarec's memory accesses much faster.
extern u32 __ctru_heap, __ctru_heap_size, __ctru_linear_heap, __ctru_linear_heap_size;
extern char *fake_heap_start, *fake_heap_end;
void mappableInit(u32 addrMin, u32 addrMax);

#define MB (1024 * 1024)

void __system_allocateHeaps(void)
{
	Handle reslimit = 0;
	s64 max_commit = 0, cur_commit = 0;
	ResourceLimitType type = RESLIMIT_COMMIT;

	if (R_FAILED(svcGetResourceLimit(&reslimit, CUR_PROCESS_HANDLE)))
		svcBreak(USERBREAK_PANIC);
	svcGetResourceLimitLimitValues(&max_commit, reslimit, &type, 1);
	svcGetResourceLimitCurrentValues(&cur_commit, reslimit, &type, 1);
	svcCloseHandle(reslimit);

	u32 remaining = (u32)(max_commit - cur_commit) & ~0xFFF;
	u32 reserve = remaining > 80 * MB ? 12 * MB : 8 * MB;
	u32 linear = remaining / 3;
	if (linear > 24 * MB) linear = 24 * MB;
	if (linear < 12 * MB) linear = 12 * MB;
	u32 heap = remaining > linear + reserve + 8 * MB ? remaining - linear - reserve : 8 * MB;
	if (heap > 64 * MB) heap = 64 * MB;
	if (heap + linear > remaining)
		heap = remaining - linear;   // tiny console: give up the reserve
	__ctru_heap_size = heap & ~0xFFF;
	__ctru_linear_heap_size = linear & ~0xFFF;

	if (R_FAILED(svcControlMemory(&__ctru_heap, OS_HEAP_AREA_BEGIN, 0x0, __ctru_heap_size,
			MEMOP_ALLOC, MEMPERM_READ | MEMPERM_WRITE)))
		svcBreak(USERBREAK_PANIC);
	if (R_FAILED(svcControlMemory(&__ctru_linear_heap, 0x0, 0x0, __ctru_linear_heap_size,
			MEMOP_ALLOC_LINEAR, MEMPERM_READ | MEMPERM_WRITE)))
		svcBreak(USERBREAK_PANIC);
	mappableInit(OS_MAP_AREA_BEGIN, OS_MAP_AREA_END);

	fake_heap_start = (char *)__ctru_heap;
	fake_heap_end = fake_heap_start + __ctru_heap_size;
}

// Set when we may use the kernel calls the dynarec needs (Luma3DS gives
// homebrew full SVC access). Without it the core uses the slow interpreter.
int __ctr_svchax;
bool g_new3ds;

static bool detect_svc_access(void)
{
	s64 out = 0;
	// SystemInfo type 0x10000 is a Luma3DS extension; it errors elsewhere.
	return R_SUCCEEDED(svcGetSystemInfo(&out, 0x10000, 0));
}

// Citra/Azahar answer SystemInfo type 0x20000 ("is emulator"); real consoles
// return an error. Those emulators can't run Luma's cache-flush kernel call,
// so the dynarec's freshly written code goes stale and crashes there.
static bool running_in_emulator(void)
{
	s64 out = 0;
	// Citra reports 1, Azahar 2
	return R_SUCCEEDED(svcGetSystemInfo(&out, 0x20000, 0)) && out > 0;
}

static void draw_message(void *ud)
{
	ui_text(10, 10, 0.5f, C2D_Color32(255, 255, 255, 255), "%s", (const char *)ud);
	ui_text(10, 200, 0.45f, C2D_Color32(170, 170, 180, 255), "Press any button");
}

static void show_error(const char *msg)
{
	log_printf("error: %s\n", msg);
	while (aptMainLoop()) {
		input_poll();
		if (input_keys_down())
			break;
		ui_begin();
		video_present_vsync(false, NULL, draw_message, (void *)msg);
	}
}

// Diagnostic switches, passed as arguments (3dslink ... -- nonet nosound):
// each turns one subsystem off so a problem can be bisected without
// rebuilding.
bool sw_nonet, sw_nosound, sw_noirrst, sw_noaa, sw_nolcd, sw_nocovers;

static void parse_switches(int argc, char **argv)
{
	for (int i = 1; i < argc; i++) {
		const char *a = argv[i];
		if (!strcmp(a, "nonet")) sw_nonet = true;
		else if (!strcmp(a, "nosound")) sw_nosound = true;
		else if (!strcmp(a, "noirrst")) sw_noirrst = true;
		else if (!strcmp(a, "noaa")) sw_noaa = true;
		else if (!strcmp(a, "nolcd")) sw_nolcd = true;
		else if (!strcmp(a, "nocovers")) sw_nocovers = true;
		else if (!strcmp(a, "minimal"))
			sw_nonet = sw_nosound = sw_noirrst = sw_noaa = sw_nolcd = sw_nocovers = true;
	}
}

int main(int argc, char **argv)
{
	char path[PATH_LEN];
	parse_switches(argc, argv);

	// New 3DS: 804 MHz + L2 cache
	osSetSpeedupEnable(true);
	if (!sw_noirrst)
		irrstInit();
	// the core asks cfg:u for the console model to count CPU cores; without
	// it, it assumes one core and never starts its graphics thread
	cfguInit();

	mkdir(PSX3D_DIR, 0777);
	mkdir(PSX3D_BIOS_DIR, 0777);
	mkdir(PSX3D_SAVE_DIR, 0777);
	mkdir(PSX3D_STATE_DIR, 0777);
	log_init();
	log_printf("PSX3D " PSX3D_VERSION "\n");
	{
		APT_CheckNew3DS(&g_new3ds);
		log_printf("%s 3DS, heap %luMB, linear %luMB (%luMB free)\n", g_new3ds ? "New" : "Old",
			__ctru_heap_size / MB, __ctru_linear_heap_size / MB, linearSpaceFree() / MB);
	}

	__ctr_svchax = detect_svc_access();
	if (__ctr_svchax && running_in_emulator()) {
		log_printf("running in Citra/Azahar: dynarec disabled\n");
		__ctr_svchax = 0;
	}
	log_printf("svc access: %d\n", __ctr_svchax);
	log_printf("switches: nonet=%d nosound=%d noirrst=%d noaa=%d nolcd=%d nocovers=%d\n",
		sw_nonet, sw_nosound, sw_noirrst, sw_noaa, sw_nolcd, sw_nocovers);

	config_load();
	if (!video_init()) {
		log_printf("video_init failed\n");
		return 1;
	}
	romfsInit();
	ui_init();
	libui_init();
	if (!sw_nocovers)
		covers_init();
	if (!sw_nosound)
		uisound_init();
	emu_init();

	// Home screen: the game library (needs a games folder); Y switches to
	// the folder browser, which also is where a games folder gets picked.
	bool use_library = g_cfg.games_dir[0] && library_load(g_cfg.games_dir, false);
	if (use_library && g_cfg.last_title[0])
		libui_select_title(g_cfg.last_title);
	for (;;) {
		bool play;
		const game_t *picked = NULL;
		if (use_library) {
			int r = libui_run(path, sizeof(path));
			if (r == 0)
				break;
			if (r == 2) {
				play = browser_run(path, sizeof(path));
				if (!play) {
					// back from the browser: the games folder may have changed
					use_library = g_cfg.games_dir[0] && library_load(g_cfg.games_dir, false);
					continue;
				}
			} else {
				play = true;
				picked = libui_picked();
			}
		} else {
			play = browser_run(path, sizeof(path));
			if (!play) {
				if (g_cfg.games_dir[0] && library_load(g_cfg.games_dir, false)) {
					use_library = true;
					continue;
				}
				break;
			}
		}
		// library games save under disc 1's name, same as starting that
		// disc from the folder browser
		if (!emu_run_game(path, picked ? picked->cover_key : NULL, picked ? picked->title : NULL))
			show_error("Could not load that game.\nCheck sdmc:/3ds/PSX3D/log.txt");
		if (aptShouldClose())
			break;
	}

	config_save();
	if (!sw_nosound)
		uisound_exit();
	if (!sw_nocovers)
		covers_exit();
	libui_exit();
	romfsExit();
	emu_deinit();
	ui_exit();
	video_exit();
	if (!sw_noirrst)
		irrstExit();
	cfguExit();
	log_close();
	return 0;
}
