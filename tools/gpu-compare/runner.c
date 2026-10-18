// Headless libretro runner for profiling the PSX3D core under qemu-arm.
//   runner <game> <frames> [state|-] [options.ini] [out.ppm]
// Runs <frames> frames (after loading the state, if given) and optionally
// writes the last frame as a PPM.
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include "libretro.h"
void psx3d_set_half3d(int on);
extern int psx3d_prof_on;
extern unsigned long long psx3d_prof_t[];
extern unsigned int psx3d_prof_c[];

static char opt_k[128][96], opt_v[128][64];
static int nopt;
static const void *last_fb;
static unsigned last_w, last_h;
static size_t last_pitch;
static unsigned long frames_shown;

static void core_log(enum retro_log_level level, const char *fmt, ...)
{
	va_list ap;
	(void)level;
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
}

static bool env(unsigned cmd, void *data)
{
	switch (cmd) {
	case RETRO_ENVIRONMENT_GET_CAN_DUPE: *(bool *)data = true; return true;
	case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
	case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY: *(const char **)data = "."; return true;
	case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT:
		return *(enum retro_pixel_format *)data == RETRO_PIXEL_FORMAT_RGB565;
	case RETRO_ENVIRONMENT_GET_VARIABLE: {
		struct retro_variable *v = data;
		for (int i = 0; i < nopt; i++)
			if (!strcmp(opt_k[i], v->key)) { v->value = opt_v[i]; return true; }
		v->value = NULL;
		return false;
	}
	case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE: *(bool *)data = false; return true;
	case RETRO_ENVIRONMENT_GET_INPUT_BITMASKS: return true;
	case RETRO_ENVIRONMENT_GET_CURRENT_SOFTWARE_FRAMEBUFFER: {
		// like PSX3D: hand the core our buffer (1024 px pitch)
		static uint16_t fbs[2][1024 * 512];
		static int cur;
		struct retro_framebuffer *fb = data;
		if (!getenv("SWFB") || fb->width > 1024 || fb->height > 512) return false;
		fb->data = fbs[cur ^= 1];
		fb->pitch = 1024 * 2;
		fb->format = RETRO_PIXEL_FORMAT_RGB565;
		return true;
	}
	case RETRO_ENVIRONMENT_GET_LOG_INTERFACE:
		((struct retro_log_callback *)data)->log = core_log;
		return true;
	}
	return false;
}

static void video(const void *data, unsigned w, unsigned h, size_t pitch)
{
	frames_shown++;
	if (data) { last_fb = data; last_w = w; last_h = h; last_pitch = pitch; }
}
static void audio1(int16_t l, int16_t r) { (void)l; (void)r; }
static size_t audiob(const int16_t *d, size_t n) { (void)d; return n; }
static void poll(void) {}
static unsigned input_mask;
static struct { int first, last; unsigned mask; } input_steps[128];
static int nsteps;
static void input_frame(int frame)
{
	if (!nsteps) return;
	input_mask = 0;
	for (int i = 0; i < nsteps; i++)
		if (frame >= input_steps[i].first && frame <= input_steps[i].last)
			input_mask |= input_steps[i].mask;
}
static int16_t state(unsigned p, unsigned d, unsigned i, unsigned id)
{
	(void)i;
	if (p || d != RETRO_DEVICE_JOYPAD) return 0;
	if (id == RETRO_DEVICE_ID_JOYPAD_MASK) return input_mask;
	return id < 16 && ((input_mask >> id) & 1);
}

static void *read_all(const char *path, size_t *len)
{
	FILE *f = fopen(path, "rb");
	if (!f) return NULL;
	fseek(f, 0, SEEK_END); *len = ftell(f); fseek(f, 0, SEEK_SET);
	void *b = malloc(*len);
	if (fread(b, 1, *len, f) != *len) { free(b); b = NULL; }
	fclose(f);
	return b;
}

int main(int argc, char **argv)
{
	if (argc < 3) { fprintf(stderr, "usage: runner game frames [state|-] [opts] [out.ppm]\n"); return 1; }
	int frames = atoi(argv[2]);
	if (getenv("INPUT_MASK")) input_mask = strtoul(getenv("INPUT_MASK"), NULL, 0);
	if (getenv("INPUT_SCRIPT")) {
		FILE *f = fopen(getenv("INPUT_SCRIPT"), "r");
		if (!f) { perror("INPUT_SCRIPT"); return 1; }
		while (nsteps < 128 && fscanf(f, "%d %d %x", &input_steps[nsteps].first,
			&input_steps[nsteps].last, &input_steps[nsteps].mask) == 3) nsteps++;
		fclose(f);
	}
	if (argc > 4) {
		FILE *f = fopen(argv[4], "r");
		char line[200];
		while (f && fgets(line, sizeof(line), f) && nopt < 128) {
			char *eq = strchr(line, '=');
			if (!eq || line[0] == '#') continue;
			*eq = 0; eq[strcspn(eq + 1, "\r\n") + 1] = 0;
			snprintf(opt_k[nopt], 96, "%s", line);
			snprintf(opt_v[nopt], 64, "%s", eq + 1);
			nopt++;
		}
		if (f) fclose(f);
	}
	retro_set_environment(env);
	retro_set_video_refresh(video);
	retro_set_audio_sample(audio1);
	retro_set_audio_sample_batch(audiob);
	retro_set_input_poll(poll);
	retro_set_input_state(state);
	retro_init();
	struct retro_game_info gi = { .path = argv[1] };
	if (!retro_load_game(&gi)) { fprintf(stderr, "load failed\n"); return 1; }
	if (argc > 3 && strcmp(argv[3], "-")) {
		size_t len, want = retro_serialize_size();
		void *s = read_all(argv[3], &len);
		if (!s || !retro_unserialize(s, len < want ? len : want)) { fprintf(stderr, "state load failed\n"); return 1; }
		fprintf(stderr, "state loaded (%zu bytes)\n", len);
	}
	// The simulator is not built with _3DS, which guards the frontend's
	// widescreen option reader. Exercise the same GTE scale explicitly.
	for (int i = 0; i < nopt; i++)
		if (!strcmp(opt_k[i], "pcsx_rearmed_psx3d_widescreen")) {
			extern int gte_hack_widescreen_q16;
			gte_hack_widescreen_q16 = strtol(opt_v[i], NULL, 10);
			fprintf(stderr, "GTE widescreen Q16: %d\n", gte_hack_widescreen_q16);
		}
	{ void trace_state(void); trace_state(); }
	for (int i = getenv("SHIFT") ? atoi(getenv("SHIFT")) : 0; i > 0; i--)
		retro_run();   // shift frame parity against the game's flips
	psx3d_prof_on = 1;
	psx3d_prof_t[5] = psx3d_prof_c[5] = 0;
	struct timespec t0, t1;
	clock_gettime(CLOCK_MONOTONIC, &t0);
	for (int i = 0; i < frames; i++) {
		input_frame(i + 1);
		retro_run();
		{ void trace_frame(int); trace_frame(i + 1); }
	}
	clock_gettime(CLOCK_MONOTONIC, &t1);
	fprintf(stderr, "gpu words/frame drawn %llu skipped %u\n", psx3d_prof_t[5] / frames, psx3d_prof_c[5] / frames);
	fprintf(stderr, "ran %d frames (%lu shown) in %.2fs\n", frames, frames_shown,
		(t1.tv_sec - t0.tv_sec) + (t1.tv_nsec - t0.tv_nsec) / 1e9);
	if (argc > 5 && last_fb) {
		FILE *f = fopen(argv[5], "wb");
		fprintf(f, "P6 %u %u 255\n", last_w, last_h);
		for (unsigned y = 0; y < last_h; y++) {
			const uint16_t *row = (const uint16_t *)((const uint8_t *)last_fb + y * last_pitch);
			for (unsigned x = 0; x < last_w; x++) {
				uint16_t p = row[x];
				unsigned char rgb[3] = { (p >> 11) << 3, ((p >> 5) & 63) << 2, (p & 31) << 3 };
				fwrite(rgb, 1, 3, f);
			}
		}
		fclose(f);
	}
	if (getenv("VRAMDUMP")) {   // gpulib's VRAM pointer follows its 16 registers
		extern unsigned char gpu[];
		uint16_t *vram = *(uint16_t **)(gpu + 64);
		FILE *f = fopen(getenv("VRAMDUMP"), "wb");
		fwrite(vram, 2, 1024 * 512, f);
		fclose(f);
	}
	retro_unload_game();
	retro_deinit();
	return 0;
}
