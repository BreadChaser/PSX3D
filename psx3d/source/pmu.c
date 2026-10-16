// ARM11 MPCore performance monitor: a cycle counter and two event counters,
// through the kernel (svcControlPerformanceCounter), which owns them: writing
// the CP15 registers directly behind its back crashed the console on leaving
// PSX3D. Test tool: measures what the emulator does on the real CPU (cache and
// TLB misses, mispredicted branches, stalls) at a few system calls per frame.
#include "psx3d.h"

static const u32 regs[3] = {
	PERFCOUNTERREG_CORE_CYCLE_COUNTER, PERFCOUNTERREG_CORE_COUNT_REG_0, PERFCOUNTERREG_CORE_COUNT_REG_1
};
static bool on;
static u64 last[3];
static u64 sum[3];
static unsigned frames;

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
	frames = 0;
	sum[0] = sum[1] = sum[2] = 0;
	log_printf("pmu: events %02x %02x\n", ev0, ev1);
}

void pmu_begin(void)
{
	if (on)
		read_all(last);
}

void pmu_end(void)
{
	if (!on)
		return;
	u64 v[3];
	read_all(v);
	for (int i = 0; i < 3; i++)
		sum[i] += v[i] - last[i];
	frames++;
}

// hand the counters back to the kernel: the system must never run with them on
void pmu_stop(void)
{
	if (!on)
		return;
	u64 o;
	svcControlPerformanceCounter(&o, PERFCOUNTEROP_DISABLE, 0, 0);
	on = false;
}

// once a second: per frame averages
void pmu_report(void)
{
	if (!on || !frames)
		return;
	log_printf("pmu: per frame %.3fM cycles, ev0 %.1fk, ev1 %.1fk\n",
		sum[0] / (double)frames / 1e6, sum[1] / (double)frames / 1e3, sum[2] / (double)frames / 1e3);
	sum[0] = sum[1] = sum[2] = 0;
	frames = 0;
}
