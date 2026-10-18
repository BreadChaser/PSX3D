// ARM11 MPCore performance monitor: a cycle counter and two event counters,
// through the kernel (svcControlPerformanceCounter), which owns them: writing
// the CP15 registers directly behind its back crashed the console on leaving
// PSX3D. Test tool: measures what the emulator does on the real CPU (cache and
// TLB misses, mispredicted branches, stalls) at a few system calls per frame.
#include <string.h>
#include "psx3d.h"
#include "devtools.h"
#include "../../core/plugins/gpulib/psx3d_frameskip.h"

static const u32 regs[3] = {
	PERFCOUNTERREG_CORE_CYCLE_COUNTER, PERFCOUNTERREG_CORE_COUNT_REG_0, PERFCOUNTERREG_CORE_COUNT_REG_1
};
static bool on;
static u64 last[3];
static u64 sum[3];
static unsigned frames, phase_prims, phase_skipped;
static u64 category_cycles[3];
static u64 class_totals[3][7], class_audio;
static unsigned class_spu, class_wait, class_pica, class_dma;
static unsigned category_count[3];
static u64 render_last[3], render_sum[3];
static unsigned render_samples, render_rng = 1;
static bool render_active;

// Sample 1/16 command lists. Only enabled for a main-thread GPU renderer;
// kernel virtual PMU values do not describe the separate New 3DS GPU worker.
static void renderer_boundary(int end)
{
 if (!end) {
  render_rng ^= render_rng << 13; render_rng ^= render_rng >> 17; render_rng ^= render_rng << 5;
  render_active = !(render_rng & 15);
  if (render_active) for (int k=0;k<3;k++)
   svcControlPerformanceCounter(&render_last[k], PERFCOUNTEROP_GET_VALUE, regs[k], 0);
 } else if (render_active) {
  for (int k=0;k<3;k++) {
   u64 v; svcControlPerformanceCounter(&v, PERFCOUNTEROP_GET_VALUE, regs[k], 0);
   render_sum[k] += v-render_last[k];
  }
  render_samples++;
 }
}

void pmu_renderer(double values[4])
{
 for (int k=0;k<4;k++) values[k] = -1;
 if (!on || !pica_pmu || !frames) return;
 for (int k=0;k<3;k++) values[k] = render_sum[k] * 16.0 / frames / (k ? 1e3 : 1e6);
 values[3] = render_samples;
}

static void read_all(u64 *v)
{
	for (int i = 0; i < 3; i++)
		svcControlPerformanceCounter(&v[i], PERFCOUNTEROP_GET_VALUE, regs[i], 0);
}

// events (PerfCounterEvent in libctru): 0x00 I-cache miss, 0x01 instruction
// starved stall, 0x02 data hazard stall, 0x03/0x04 instruction/data micro TLB
// miss, 0x05 branches, 0x07 mispredicted, 0x08 instructions, 0x0a/0x0b D-cache
// reads/read misses, 0x0c/0x0d writes/write misses, 0x10 main TLB miss,
// 0x12 load/store unit full stall, 0xff cycles
void pmu_start(int ev0, int ev1)
{
	u64 o;
	Result r = svcControlPerformanceCounter(&o, PERFCOUNTEROP_ENABLE, 0, 0);
	if (R_FAILED(r)) {
		log_printf("pmu: enable failed %08lx\n", r);
		return;
	}
	svcControlPerformanceCounter(&o, PERFCOUNTEROP_SET_VIRTUAL_COUNTER_ENABLED, 1, 0);
	svcControlPerformanceCounter(&o, PERFCOUNTEROP_SET_EVENT, PERFCOUNTERREG_CORE_CYCLE_COUNTER,
		PERFCOUNTEREVT_CORE_CYCLE_COUNT);
	svcControlPerformanceCounter(&o, PERFCOUNTEROP_SET_EVENT, PERFCOUNTERREG_CORE_COUNT_REG_0, ev0 & 0xff);
	svcControlPerformanceCounter(&o, PERFCOUNTEROP_SET_EVENT, PERFCOUNTERREG_CORE_COUNT_REG_1, ev1 & 0xff);
	svcControlPerformanceCounter(&o, PERFCOUNTEROP_RESET, 7, 7);
	on = true;
 memset(render_sum, 0, sizeof(render_sum)); render_samples = 0; render_active = false; render_rng = 1;
 pica_pmu = sw_pmuphase && renderer_is_gpu() &&
  strcmp(options_get("pcsx_rearmed_gpu_thread_rendering"), "enabled") ? renderer_boundary : NULL;
	frames = 0;
	memset(class_totals, 0, sizeof(class_totals));
	memset(category_cycles, 0, sizeof(category_cycles));
	memset(category_count, 0, sizeof(category_count));
	sum[0] = sum[1] = sum[2] = 0;
	log_printf("pmu: events %02x %02x\n", ev0, ev1);
}

void pmu_begin(void)
{
	if (on)
		read_all(last);
	if (on && sw_pmuclass) {
  class_dma = psx3d_skip.dma_ticks;
  class_spu = psx3d_skip.spu_main_ticks;
  class_wait = psx3d_skip.spu_wait_ticks;
  class_pica = psx3d_skip.gpu_main_ticks;
  class_audio = emu_audio_ticks();
 }
	phase_prims = psx3d_skip.render_prims;
	phase_skipped = psx3d_skip.words_skipped;
}

void pmu_end(void)
{
	if (!on)
		return;
	u64 v[3];
	read_all(v);
	for (int i = 0; i < 3; i++)
		sum[i] += v[i] - last[i];
	unsigned kind = psx3d_skip.render_prims != phase_prims ? 0 : psx3d_skip.words_skipped != phase_skipped ? 1 : 2;
	category_cycles[kind] += v[0] - last[0];
	category_count[kind]++;
 if (sw_pmuclass) {
  class_totals[kind][0] += v[1] - last[1];
  class_totals[kind][1] += v[2] - last[2];
  class_totals[kind][2] += (unsigned)(psx3d_skip.spu_main_ticks - class_spu);
  class_totals[kind][3] += (unsigned)(psx3d_skip.spu_wait_ticks - class_wait);
  class_totals[kind][4] += (unsigned)(psx3d_skip.gpu_main_ticks - class_pica);
  class_totals[kind][5] += emu_audio_ticks() - class_audio;
  class_totals[kind][6] += (unsigned)(psx3d_skip.dma_ticks - class_dma);
 }
	frames++;
}

// hand the counters back to the kernel: the system must never run with them on
void pmu_stop(void)
{
 pica_pmu = NULL;
	if (!on)
		return;
	u64 o;
	svcControlPerformanceCounter(&o, PERFCOUNTEROP_DISABLE, 0, 0);
	on = false;
}

bool pmu_sample(double *cycles, double *ev0, double *ev1)
{
	*cycles = *ev0 = *ev1 = -1;
	if (!on || !frames)
		return false;
	*cycles = sum[0] / (double)frames / 1e6;
	*ev0 = sum[1] / (double)frames / 1e3;
	*ev1 = sum[2] / (double)frames / 1e3;
	return true;
}

// once a second: per frame averages
void pmu_report(void)
{
	if (!on || !frames)
		return;
	if (!dev_enabled() || log_linked() || sw_prof || sw_stats)
		log_printf("pmu: per frame %.3fM cycles, ev0 %.1fk, ev1 %.1fk\n",
		sum[0] / (double)frames / 1e6, sum[1] / (double)frames / 1e3, sum[2] / (double)frames / 1e3);
	sum[0] = sum[1] = sum[2] = 0;
 memset(render_sum, 0, sizeof(render_sum)); render_samples = 0;
	frames = 0;
	memset(class_totals, 0, sizeof(class_totals));
	memset(category_cycles, 0, sizeof(category_cycles));
	memset(category_count, 0, sizeof(category_count));
}

void pmu_categories(double cycles[3], unsigned counts[3])
{
 for (int k=0; k<3; k++) {
  counts[k] = category_count[k];
  cycles[k] = counts[k] ? category_cycles[k] / (double)counts[k] / 1e6 : -1;
 }
}

// Normalized within each population, with the same classification as cycles.
// SPU/PICA/audio are wall ticks nested inside retro_run, not additive PMU work.
void pmu_category_detail(double values[21])
{
 for (unsigned k=0; k<3; k++) for (unsigned j=0; j<7; j++)
  values[k*7+j] = on && sw_pmuclass && category_count[k]
   ? class_totals[k][j] / (double)category_count[k] / (j<2 ? 1000.0 : CPU_TICKS_PER_MSEC) : -1;
}
