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
#include "clip.h"
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

static retro_audio_buffer_status_callback_t buffer_status;
static char opt_k[128][96], opt_v[128][64];
static int nopt;
static const void *last_fb;
static unsigned skipped_shown;
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
	case RETRO_ENVIRONMENT_SET_AUDIO_BUFFER_STATUS_CALLBACK:
        buffer_status=data ? ((struct retro_audio_buffer_status_callback *)data)->callback : NULL; return true;
    case RETRO_ENVIRONMENT_GET_CAN_DUPE: *(bool *)data = true; return true;
	case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
	case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY: *(const char **)data = "."; return true;
	case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT:
		return *(enum retro_pixel_format *)data == RETRO_PIXEL_FORMAT_RGB565;
	case RETRO_ENVIRONMENT_GET_VARIABLE: {
		struct retro_variable *v = data;
		const char *recorded=clip_option(v->key);
		if (recorded) { v->value=recorded; return true; }
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
	if (!data) skipped_shown++;
	if (data) { last_fb = data; last_w = w; last_h = h; last_pitch = pitch; }
}
static void audio1(int16_t l, int16_t r) { (void)l; (void)r; }
static size_t audiob(const int16_t *d, size_t n) { (void)d; return n; }
static unsigned input_frame;
static void poll(void) { input_frame++; }

static int16_t state(unsigned p, unsigned d, unsigned i, unsigned id)
{
    if (!getenv("CLIP_INPUT") || p) return 0;
    // Exercise moving gameplay and changing digital/analog inputs, not an idle state.
    unsigned phase=(input_frame/30)%4;
    unsigned mask=(1u<<RETRO_DEVICE_ID_JOYPAD_UP);
    if (phase==1) mask|=1u<<RETRO_DEVICE_ID_JOYPAD_B;
    if (phase==2) mask|=1u<<RETRO_DEVICE_ID_JOYPAD_RIGHT;
    if (d==RETRO_DEVICE_JOYPAD) return id==RETRO_DEVICE_ID_JOYPAD_MASK ? mask : id<16 && ((mask>>id)&1);
    if (d==RETRO_DEVICE_ANALOG && i<2 && id<2) return phase%2 ? 16384 : -16384;
    return 0;
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
	if (getenv("CLIP_REPLAY") && !clip_open(getenv("CLIP_REPLAY"),NULL)) {
        fprintf(stderr,"clip: %s\n",clip_status.message); return 2;
    }
    if (getenv("CLIP_IDLE")) { extern int psx3d_idle_skip; psx3d_idle_skip=atoi(getenv("CLIP_IDLE")); }
    retro_set_environment(env);
	retro_set_video_refresh(video);
	retro_set_audio_sample(audio1);
	retro_set_audio_sample_batch(audiob);
	retro_set_input_poll(poll);
	retro_set_input_state(state);
	retro_init();
	retro_set_controller_port_device(0,RETRO_DEVICE_SUBCLASS(RETRO_DEVICE_ANALOG,1));
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
	{ extern int psx3d_chain_batch; if (getenv("NOBATCH")) psx3d_chain_batch = 0; }
#endif
	if (!getenv("CLIP_REPLAY") && argc > 3 && strcmp(argv[3], "-")) {
		size_t len, want = retro_serialize_size();
		void *s = read_all(argv[3], &len);
		if (!s || !retro_unserialize(s, len < want ? len : want)) { fprintf(stderr, "state load failed\n"); return 1; }
		fprintf(stderr, "state loaded (%zu bytes)\n", len);
#ifdef GLES
		glc3d_frame_started();
		psx3d_gles_present_vram();
#endif
	}
	clip_set_live_input(poll,state);
    if (getenv("CLIP_REPLAY")) {
        if (!clip_restore()) { fprintf(stderr,"clip: %s\n",clip_status.message); return 2; }
        frames=clip_status.frames;
    } else if (getenv("CLIP_RECORD")) {
        char settings[CLIP_META-128]=""; size_t used=0;
        for (int k=0;k<nopt;k++) used+=snprintf(settings+used,sizeof(settings)-used,"%s=%s\n",opt_k[k],opt_v[k]);
        extern char CdromId[];
        if (!clip_record(getenv("CLIP_RECORD"),frames,60000,CdromId,settings)) {
            fprintf(stderr,"clip: %s\n",clip_status.message); return 2;
        }
    }
    if (getenv("HALF3D")) psx3d_set_half3d(1);
	for (int i = getenv("SHIFT") ? atoi(getenv("SHIFT")) : 0; i > 0; i--)
		retro_run();   // shift frame parity against the game's flips
	psx3d_prof_on = 1;
	psx3d_prof_t[5] = psx3d_prof_c[5] = 0;
	struct timespec t0, t1;
	clock_gettime(CLOCK_MONOTONIC, &t0);
	for (int i = 0; i < frames; i++) {
#ifdef GLES
		glc3d_frame_started();
#endif
#ifdef PICA
		pica_frame_started();
#endif
        if (buffer_status) buffer_status(true,20,getenv("CLIP_SKIP") && (i%3!=0));
        if (clip_active) clip_run(); else retro_run();
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
	clock_gettime(CLOCK_MONOTONIC, &t1);
	fprintf(stderr, "gpu words/frame drawn %llu skipped %u\n", psx3d_prof_t[5] / frames, psx3d_prof_c[5] / frames);
	fprintf(stderr, "ran %d frames (%lu shown, %u duplicate callbacks) in %.2fs\n", frames, frames_shown, skipped_shown,
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
	if (clip_status.frames) fprintf(stderr,"clip: %s; frames=%u checks=%u first_divergent=%u\n",
        clip_status.message,clip_status.frame,clip_status.checks,clip_status.first_divergent);
    bool clip_failed=clip_status.failed;
    clip_close();
    retro_unload_game();
	retro_deinit();
	return clip_failed ? 2 : 0;
}
