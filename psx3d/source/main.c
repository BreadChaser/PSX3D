#define _GNU_SOURCE   // strcasestr
// PSX3D entry point.
#include <stdio.h>
#include <stdlib.h>
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
	// RAM 0x210000 + scratch 0x10000 + BIOS 0x80000 + LUTs 0x800000
	// need 10.625 MiB outside the heaps. An 8 MiB reserve made the LUT
	// mapping fall back to calloc, taking 8 MiB from the save-state heap.
	// With the 8 MiB static dynarec cache the Old 3DS can afford 12 MiB.
	u32 reserve = 12 * MB;
	u32 linear = remaining / 3;
	if (linear > 24 * MB) linear = 24 * MB;
	// GPU atlas/staging/UI consume ~13.5 MiB before core startup. Leave
	// another ~3 MiB for core VRAM/vout and ~1.4 MiB for eleven thumbnails.
	if (linear < 19 * MB) linear = 19 * MB;
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
int g_syscore_pct;   // syscore (core 1) time granted to us, percent

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
float sw_slider = -1;   // test: pretend the 3D slider is here
bool sw_showright;      // test: show the right eye on the left (NTR only sees the left)
bool sw_sbs;            // test: both eyes side by side
bool sw_prof;           // log where frame time goes, once a second
bool sw_stats;          // log fps and renderer counters once a second (no timers)
bool sw_o3ds;           // test: behave like an Old 3DS (settings not saved)
bool sw_fs3;            // test: Old 3DS auto frameskip may skip up to 3 in a row
bool sw_fs1;            // test: force frameskip, every other frame
bool sw_noskip;         // test: no frameskip (benchmarks)
bool sw_nosputhread;    // test: Old 3DS: sound mixing on the main core
bool sw_acap;           // test: log a checksum of 10s of the core's audio, from frame 60
char sw_opt[8][48];     // test: o:<option>=<value> (pcsx_rearmed_ left out), up to 8
char sw_cdra[8];        // test: Old 3DS: CD read-ahead sectors (on the syscore)
char sw_clock[8];       // test: emulated PS1 CPU clock (pcsx_rearmed_psxclock), e.g. 40
int sw_gldbg;           // test: hardware renderer debug bits (1 no alpha test, 2 no textures)
char sw_game[64];       // test: start the first library game whose title contains this
int sw_vramdump;         // test: dump PS1 VRAM this many frames after the game starts
int sw_pmu0 = -1, sw_pmu1 = -1;   // test: ARM11 performance counter events (pmu=AA,BB, hex)
int sw_state = -1;       // test: load this save state slot when the game starts (state0, state=N)
bool sw_nogputhread;
bool sw_gpu_async = true;
bool sw_gpu_flush = true;
int sw_renderer = -1;    // renderer=sw|gpu overrides global/per-game settings
bool sw_aa;              // test: Old 3DS: gameplay supersampled too
bool sw_csnd, sw_nonet, sw_nosound, sw_noirrst, sw_noaa, sw_nolcd, sw_nocovers;

static void parse_switches(int argc, char **argv)
{
	for (int i = 1; i < argc; i++) {
		const char *a = argv[i];
		if (!strcmp(a, "nonet")) sw_nonet = true;
		else if (!strcmp(a, "csnd")) sw_csnd = true;   // test the no-firmware sound path
		else if (!strncmp(a, "slider=", 7)) sw_slider = strtof(a + 7, NULL);
		else if (!strcmp(a, "showright")) sw_showright = true;
		else if (!strcmp(a, "sbs")) sw_sbs = true;
		else if (!strcmp(a, "prof")) sw_prof = true;
		else if (!strcmp(a, "stats")) sw_stats = true;
		else if (!strcmp(a, "o3ds")) sw_o3ds = true;
		else if (!strcmp(a, "fs1")) sw_fs1 = true;
		else if (!strcmp(a, "fs3")) sw_fs3 = true;
		else if (!strcmp(a, "noskip")) sw_noskip = true;
		else if (!strcmp(a, "nogputhread")) sw_nogputhread = true;
		else if (!strcmp(a, "gputhread")) sw_nogputhread = false;
		else if (!strcmp(a, "nosputhread")) sw_nosputhread = true;
		else if (!strcmp(a, "acap")) sw_acap = true;
		else if (!strcmp(a, "renderer=sw")) sw_renderer = 1;
		else if (!strcmp(a, "renderer=gpu")) sw_renderer = 0;
		else if (!strcmp(a, "state0")) sw_state = 0;
		else if (!strncmp(a, "state=", 6)) sw_state = atoi(a + 6);
		else if (!strcmp(a, "rtt=old")) sw_gldbg |= 262144;
		else if (!strcmp(a, "rtt=new")) sw_gldbg &= ~262144;
		else if (!strncmp(a, "gldbg=", 6)) sw_gldbg = atoi(a + 6);
		else if (!strncmp(a, "vramdump=", 9)) sw_vramdump = atoi(a + 9);
		else if (!strncmp(a, "pmu=", 4)) sscanf(a + 4, "%x,%x", (unsigned *)&sw_pmu0, (unsigned *)&sw_pmu1);
		else if (!strncmp(a, "o:", 2)) {
			for (int k = 0; k < 8; k++)
				if (!sw_opt[k][0]) { snprintf(sw_opt[k], sizeof(sw_opt[k]), "%s", a + 2); break; }
		}
		else if (!strncmp(a, "cdra=", 5)) snprintf(sw_cdra, sizeof(sw_cdra), "%s", a + 5);
		else if (!strncmp(a, "clock=", 6)) snprintf(sw_clock, sizeof(sw_clock), "%s", a + 6);
		else if (!strncmp(a, "game=", 5)) snprintf(sw_game, sizeof(sw_game), "%s", a + 5);
		else if (!strcmp(a, "nosound")) sw_nosound = true;
		else if (!strcmp(a, "noirrst")) sw_noirrst = true;
		else if (!strcmp(a, "noaa")) sw_noaa = true;
		else if (!strcmp(a, "gpuasync")) sw_gpu_async = true;
		else if (!strcmp(a, "nogpuasync")) sw_gpu_async = false;
		else if (!strcmp(a, "gpuflush")) sw_gpu_flush = true;
		else if (!strcmp(a, "nogpuflush")) sw_gpu_flush = false;
		else if (!strcmp(a, "aa")) sw_aa = true;
		else if (!strcmp(a, "nolcd")) sw_nolcd = true;
		else if (!strcmp(a, "nocovers")) sw_nocovers = true;
		else if (!strcmp(a, "minimal"))
			sw_nonet = sw_nosound = sw_noirrst = sw_noaa = sw_nolcd = sw_nocovers = true;
	}
}

// Same switches from sdmc:/3ds/PSX3D/switches.txt (space or newline
// separated), for consoles started from the HOME Menu instead of 3dslink.
static void parse_switch_file(void)
{
	FILE *f = fopen(PSX3D_DIR "/switches.txt", "r");
	if (!f)
		return;
	char word[64];
	while (fscanf(f, "%63s", word) == 1) {
		char *argv[2] = { NULL, word };
		parse_switches(2, argv);
	}
	fclose(f);
}

float slider_get(void)
{
	return sw_slider >= 0 ? sw_slider : osGet3DSliderState();
}

int main(int argc, char **argv)
{
	char path[PATH_LEN];
	parse_switches(argc, argv);
	parse_switch_file();

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
		if (sw_o3ds)
			g_new3ds = false;   // test: Old 3DS code paths at New 3DS clock
		// borrow the syscore: as much as app.rsf allows on Old 3DS (30%), 35% on New
		{
			static const u32 want_o3ds[] = { 80, 70, 60, 50, 40, 35, 30, 25 };
			u32 pct = 0;
			if (g_new3ds) {
				Result rc = APT_SetAppCpuTimeLimit(35);
				log_printf("syscore: set 35 -> %08lx\n", rc);
			} else {
				for (size_t k = 0; k < sizeof(want_o3ds) / sizeof(want_o3ds[0]); k++) {
					Result rc = APT_SetAppCpuTimeLimit(want_o3ds[k]);
					log_printf("syscore: set %lu -> %08lx\n", want_o3ds[k], rc);
					if (R_SUCCEEDED(rc))
						break;
				}
			}
			APT_GetAppCpuTimeLimit(&pct);
			g_syscore_pct = (int)pct;
			log_printf("syscore time: %lu%%\n", pct);
		}
		log_printf("%s 3DS, heap %luMB, linear %luMB (%luMB free)\n", g_new3ds ? "New" : "Old",
			__ctru_heap_size / MB, __ctru_linear_heap_size / MB, linearSpaceFree() / MB);
	}

	__ctr_svchax = detect_svc_access();
	if (__ctr_svchax && running_in_emulator()) {
		log_printf("running in Citra/Azahar: dynarec disabled\n");
		__ctr_svchax = 0;
	}
	log_printf("svc access: %d\n", __ctr_svchax);
	if (sw_stats) {
		// test: how fast the CPU reads and writes normal vs GPU-visible (linear) memory
		const int n = 256 * 1024;
		u32 *bufs[2] = { malloc(n), linearAlloc(n) };
		for (int k = 0; k < 2; k++) {
			volatile u32 *b = bufs[k];
			if (!b)
				continue;
			u64 t0 = svcGetSystemTick();
			for (int r = 0; r < 4; r++)
				for (int i = 0; i < n / 4; i++)
					b[i] = i;
			u64 t1 = svcGetSystemTick();
			u32 sum = 0;
			for (int r = 0; r < 4; r++)
				for (int i = 0; i < n / 4; i++)
					sum += b[i];
			u64 t2 = svcGetSystemTick();
			log_printf("memtest %s: write %.2f MB/s, read %.2f MB/s (%u)\n", k ? "linear" : "heap",
				4.0 * n / ((t1 - t0) / (double)SYSCLOCK_ARM11) / 1e6, 4.0 * n / ((t2 - t1) / (double)SYSCLOCK_ARM11) / 1e6, (unsigned)sum);
		}
		free(bufs[0]);
		if (bufs[1])
			linearFree(bufs[1]);
	}
	log_printf("switches: csnd=%d nonet=%d nosound=%d noirrst=%d noaa=%d nolcd=%d nocovers=%d\n",
		sw_csnd, sw_nonet, sw_nosound, sw_noirrst, sw_noaa, sw_nolcd, sw_nocovers);

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
		dsp_firmware_ensure();   // first start: copy it from the HOME Menu
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
		if (use_library && sw_game[0]) {
			// test: straight into a game ("_" stands for a space in the switch)
			char want[64];
			snprintf(want, sizeof(want), "%s", sw_game);
			for (char *c = want; *c; c++)
				if (*c == '_') *c = ' ';
			sw_game[0] = 0;   // once
			for (int i = 0; i < library_count() && !picked; i++)
				if (strcasestr(library_get(i)->title, want))
					picked = library_get(i);
			if (picked && library_launch_path(picked, path, sizeof(path))) {
				log_printf("test: starting %s\n", picked->title);
				if (!emu_run_game(path, picked->cover_key, picked->title))
					show_error("Could not load that game.\nCheck sdmc:/3ds/PSX3D/log.txt");
				if (aptShouldClose())
					break;
				continue;
			}
			log_printf("test: no game matches %s\n", want);
			picked = NULL;
		}
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

	// each step is logged: a hang at exit shows up as the last line
	log_printf("exit: config\n");
	config_save();
	log_printf("exit: sound\n");
	if (!sw_nosound)
		uisound_exit();
	log_printf("exit: covers\n");
	if (!sw_nocovers)
		covers_exit();
	log_printf("exit: ui\n");
	libui_exit();
	romfsExit();
	emu_deinit();
	ui_exit();
	log_printf("exit: video\n");
	video_exit();
	if (!sw_noirrst)
		irrstExit();
	cfguExit();
	log_printf("exit: done\n");
	log_close();
	return 0;
}
