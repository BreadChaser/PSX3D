// PSX3D entry point.
#include <stdio.h>
#include <sys/stat.h>
#include "psx3d.h"

// Main thread stack; the core warns below 1MB (dynarec recursion).
u32 __stacksize__ = 0x100000;

// Set when we may use the kernel calls the dynarec needs (Luma3DS gives
// homebrew full SVC access). Without it the core uses the slow interpreter.
int __ctr_svchax;

static bool detect_svc_access(void)
{
	s64 out = 0;
	// SystemInfo type 0x10000 is a Luma3DS extension; it errors elsewhere.
	return R_SUCCEEDED(svcGetSystemInfo(&out, 0x10000, 0));
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
		video_present(false, NULL, draw_message, (void *)msg);
	}
}

int main(void)
{
	char path[PATH_LEN];

	// New 3DS: 804 MHz + L2 cache
	osSetSpeedupEnable(true);
	irrstInit();

	mkdir(PSX3D_DIR, 0777);
	mkdir(PSX3D_BIOS_DIR, 0777);
	mkdir(PSX3D_SAVE_DIR, 0777);
	mkdir(PSX3D_STATE_DIR, 0777);
	log_init();
	log_printf("PSX3D " PSX3D_VERSION "\n");

	__ctr_svchax = detect_svc_access();
	log_printf("svc access: %d\n", __ctr_svchax);

	config_load();
	if (!video_init()) {
		log_printf("video_init failed\n");
		return 1;
	}
	ui_init();
	emu_init();

	while (browser_run(path, sizeof(path))) {
		if (!emu_run_game(path))
			show_error("Could not load that game.\nCheck sdmc:/3ds/PSX3D/log.txt");
		if (aptShouldClose())
			break;
	}

	config_save();
	emu_deinit();
	ui_exit();
	video_exit();
	irrstExit();
	log_close();
	return 0;
}
