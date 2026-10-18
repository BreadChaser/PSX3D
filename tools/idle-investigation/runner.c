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
#include "core/libpcsxcore/r3000a.h"
#ifdef PICA
#include <stdbool.h>
bool pica_init(const void *shbin, unsigned size);
bool pica_game_begin(bool small);
void pica_frame_started(void);
void pica_frame_ended(void);
void pica_stats(char *buf, int n);
unsigned sim_c3d_draws;
#endif
#ifdef GLES
#include <stdbool.h>
long psx3d_gles_open(int w, int h);
void psx3d_gles_present_vram(void);
bool glc3d_init(void);
void glc3d_frame_started(void);
void glc3d_frame_ended(void);
unsigned sim_c3d_draws;
int sw_gldbg;
void log_printf(const char *fmt, ...) { va_list ap; va_start(ap, fmt); vfprintf(stderr, fmt, ap); va_end(ap); }
#endif
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
unsigned sim_flips;

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
static uint64_t audio_hash = 1469598103934665603ull, audio_samples;
static void audio1(int16_t l, int16_t r) {
#ifdef INV_FAST
 (void)l; (void)r; return;
#endif
 uint16_t samples[2] = {(uint16_t)l, (uint16_t)r};
 for (int i=0;i<2;i++) { audio_hash = (audio_hash ^ (samples[i]&255))*1099511628211ull; audio_hash=(audio_hash^(samples[i]>>8))*1099511628211ull; }
 audio_samples++;
}
static size_t audiob(const int16_t *d, size_t n) { for(size_t i=0;i<n;i++) audio1(d[i*2], d[i*2+1]); return n; }
static uint64_t ram_hash(void) { uint64_t h=1469598103934665603ull; for(int i=0;i<0x200000;i++) h=(h^psxRegs.ptrs.psxM[i])*1099511628211ull; return h; }
struct sim_idle_counter { unsigned pc, kind, cycles, batches, iterations; };
#ifdef INV_FAST
struct sim_idle_counter sim_idle[512];
unsigned sim_idle_n, sim_bios_calls[3][256], sim_wait_calls, sim_test_calls, sim_wait_jump;
unsigned long long sim_bios_cycles[3][256];
#endif
extern struct sim_idle_counter sim_idle[];
extern unsigned sim_idle_n, sim_bios_calls[3][256], sim_wait_calls, sim_test_calls, sim_wait_jump;
extern unsigned long long sim_bios_cycles[3][256];
static void metrics(int frame, unsigned start) {
 fprintf(stderr,"METRIC frame=%d cycles=%u ram=%016llx audio=%016llx samples=%llu\n",frame, psxRegs.cycle-start,(unsigned long long)ram_hash(),(unsigned long long)audio_hash,(unsigned long long)audio_samples);
 for(unsigned j=0;j<sim_idle_n;j++) { struct sim_idle_counter *p=sim_idle+j; fprintf(stderr,"IDLE %d %08x %u %u %u %u\n",frame,p->pc,p->kind,p->cycles,p->batches,p->iterations); }
 for(int i=0;i<3;i++) for(int j=0;j<256;j++) if(sim_bios_calls[i][j]) fprintf(stderr,"BIOS %d %c%02x %u %llu\n",frame,'A'+i,j,sim_bios_calls[i][j],sim_bios_cycles[i][j]);
 fprintf(stderr,"EVENTWAIT %d %u %u %u\n",frame,sim_wait_calls,sim_test_calls,sim_wait_jump);
}
static void poll(void) {}
static int16_t state(unsigned p, unsigned d, unsigned i, unsigned id) { (void)p; (void)d; (void)i; (void)id; return 0; }

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
	{ extern int psx3d_idle_skip, psx3d_idle_pointer, psx3d_idle_delay;
 if (getenv("NOIDLE")) psx3d_idle_skip = 0;
 psx3d_idle_pointer = getenv("IDLEPTR") != NULL;
 psx3d_idle_delay = getenv("IDLEDELAY") != NULL;
 }
	struct retro_game_info gi = { .path = argv[1] };
	if (!retro_load_game(&gi)) { fprintf(stderr, "load failed\n"); return 1; }
#ifdef GLES
	glc3d_init();
	psx3d_gles_open(400, 240);
#endif
#ifdef PICA
	static unsigned dummy_shbin[1];
	pica_init(dummy_shbin, 4);
	pica_game_begin(getenv("SMALL") != NULL);
	{ extern int pica_debug; if (getenv("PICA_DEBUG")) pica_debug = atoi(getenv("PICA_DEBUG")); }
#endif
	if (argc > 3 && strcmp(argv[3], "-")) {
		size_t len, want = retro_serialize_size();
		void *s = read_all(argv[3], &len);
		if (!s || !retro_unserialize(s, len < want ? len : want)) { fprintf(stderr, "state load failed\n"); return 1; }
		fprintf(stderr, "state loaded (%zu bytes)\n", len);
#ifdef GLES
		glc3d_frame_started();
		psx3d_gles_present_vram();
#endif
	}
	if (getenv("HALF3D")) psx3d_set_half3d(1);
	for (int i = getenv("SHIFT") ? atoi(getenv("SHIFT")) : 0; i > 0; i--)
		retro_run();   // shift frame parity against the game's flips
	psx3d_prof_on = 1;
	psx3d_prof_t[5] = psx3d_prof_c[5] = 0;
	struct timespec t0, t1;
	clock_gettime(CLOCK_MONOTONIC, &t0);
	unsigned start_cycle=psxRegs.cycle;
	for (int i = 0; i < frames; i++) {
#ifdef GLES
		glc3d_frame_started();
#endif
#ifdef PICA
		pica_frame_started();
#endif
		retro_run();
		if (getenv("CYCLOG")) fprintf(stderr,"cyc %d %u\n", i, psxRegs.cycle);
 if (!getenv("NOMETRICS") && (i==299 || i==599 || i==frames-1)) metrics(i+1,start_cycle);
#ifdef GLES
		glc3d_frame_ended();
#endif
#ifdef PICA
		{ void renderer_flush_queues(void); renderer_flush_queues(); }   // as the app's pica_picture does
		pica_frame_ended();
		if (getenv("HASHEVERY")) {
			extern unsigned long long pica_sim_hash __attribute__((weak));
			if (&pica_sim_hash) fprintf(stderr, "frame %d hash %016llx\n", i, pica_sim_hash);
			void pica_sim_mark(int) __attribute__((weak));
			if (pica_sim_mark) pica_sim_mark(i);
		}
#endif
	}
#ifdef PICA
	{ char b[300]; pica_stats(b, sizeof(b)); fprintf(stderr, "%s\nc3d draws/frame %u\n", b, sim_c3d_draws / frames); }
#endif
#ifdef GLES
	fprintf(stderr, "c3d draws/frame %u\n", sim_c3d_draws / frames);
#endif
#ifdef PICA
	{
		extern unsigned long long pica_sim_hash __attribute__((weak));
		void pica_sim_final(void) __attribute__((weak));
		if (pica_sim_final) {   // core built with PICA_SIM_HASH
			pica_sim_final();
			fprintf(stderr, "draw hash %016llx\n", pica_sim_hash);
		}
	}
#endif
	fprintf(stderr, "game flips %u\n", sim_flips);
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
	if (getenv("STATEHASH")) {   // hash of the whole savestate (CPU, cycle, RAM, VRAM, SPU, ...)
		size_t n = retro_serialize_size();
		unsigned char *b = calloc(1, n);
		unsigned long long h = 1469598103934665603ull;
		retro_serialize(b, n);
		for (size_t i = 0; i < n; i++) h = (h ^ b[i]) * 1099511628211ull;
		fprintf(stderr, "state hash %016llx (%zu bytes)\n", h, n);
		if (getenv("STATEOUT")) { FILE *f = fopen(getenv("STATEOUT"), "wb"); fwrite(b, 1, n, f); fclose(f); }
	}
	retro_unload_game();
	retro_deinit();
	return 0;
}
