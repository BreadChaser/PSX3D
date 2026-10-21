#include <string.h>
#include "audio_tsm.h"

#define DEC 4                                   // coarse search: every 4th frame
#define REF (AUDIO_TSM_FADE / DEC)              // 64 coarse reference samples
#define SPAN (2 * AUDIO_TSM_SEEK + AUDIO_TSM_FADE + DEC * 4)

void audio_tsm_reset(audio_tsm *t)
{
	t->p = t->end = 0;
	t->debt = 0;
	t->speed = 65536;
}

void audio_tsm_put(audio_tsm *t, const int16_t *in, size_t n)
{
	// Keep AUDIO_TSM_HIST frames behind the read position for jumps.
	if (t->end + (int)n > AUDIO_TSM_BUF) {
		int drop = t->p - AUDIO_TSM_HIST;
		if (drop > 0) {
			memmove(t->buf, t->buf + drop * 2, (t->end - drop) * 4);
			t->p -= drop;
			t->end -= drop;
		}
		if (t->end + (int)n > AUDIO_TSM_BUF) {  // never with the caller's sizes
			t->p = t->end = 0;
			t->debt = 0;
		}
	}
	memcpy(t->buf + t->end * 2, in, n * 4);
	t->end += n;
}

// Mono, scaled so that 64 products of two coarse samples fit 32 bits.
static inline int mono(const int16_t *f) { return (f[0] + f[1]) >> 1; }

static int coarse(const int16_t *f)
{
	return (mono(f) + mono(f + 2) + mono(f + 4) + mono(f + 6)) >> 6;
}

// Normalised match of a against b: corr * |corr| / energy(a). Float keeps
// the comparison exact enough without 128-bit products.
static float score(long long c, long long e)
{
	return (float)c * (float)(c < 0 ? -c : c) / ((float)e + 1.0f);
}

// How far back to jump from p: about want frames, to where the following
// AUDIO_TSM_FADE frames look most like the ones at p.
static int find_jump(const audio_tsm *t, int want)
{
	int lo = want - AUDIO_TSM_SEEK, hi = want + AUDIO_TSM_SEEK;
	if (lo < 64) lo = 64;
	if (hi > t->p) hi = t->p;
	if (hi < lo)
		return hi >= 64 ? hi : 0;
	const int16_t *b = t->buf;
	int ref[REF], reg[SPAN / DEC];
	for (int i = 0; i < REF; i++)
		ref[i] = coarse(b + (t->p + i * DEC) * 2);
	// Candidate start q = p - j covers [p - hi, p - lo + FADE).
	int base = t->p - hi, ncand = (hi - lo) / DEC + 1;
	for (int i = 0; i < ncand + REF - 1; i++)
		reg[i] = coarse(b + (base + i * DEC) * 2);
	int best_k = (hi - want) / DEC;
	if (best_k >= ncand) best_k = ncand - 1;
	if (best_k < 0) best_k = 0;
	float best = -3.0e38f;
	int e = 0;
	for (int i = 0; i < REF - 1; i++)
		e += reg[i] * reg[i];
	for (int k = 0; k < ncand; k++) {
		e += reg[k + REF - 1] * reg[k + REF - 1];
		int c = 0;
		for (int i = 0; i < REF; i++)
			c += ref[i] * reg[k + i];
		float s = score(c, e);
		// Ties (silence) keep the candidate nearest the wanted jump.
		if (s > best || (s == best && (k * DEC - (hi - want)) * (k * DEC - (hi - want))
		    < (best_k * DEC - (hi - want)) * (best_k * DEC - (hi - want)))) {
			best = s;
			best_k = k;
		}
		e -= reg[k] * reg[k];
	}
	// Refine at full rate around the coarse winner.
	int j0 = hi - best_k * DEC, best_j = j0;
	float fbest = -3.0e38f;
	for (int j = j0 - DEC + 1; j <= j0 + DEC - 1; j++) {
		if (j < lo || j > hi)
			continue;
		const int16_t *x = b + t->p * 2, *y = b + (t->p - j) * 2;
		long long c = 0, en = 0;
		for (int i = 0; i < AUDIO_TSM_FADE; i++) {
			int u = mono(x + i * 2) >> 4, v = mono(y + i * 2) >> 4;
			c += u * v;
			en += v * v;
		}
		float s = score(c, en);
		if (s > fbest || (s == fbest && j == j0)) {
			fbest = s;
			best_j = j;
		}
	}
	return best_j;
}

size_t audio_tsm_get(audio_tsm *t, int16_t *out, size_t max)
{
	size_t n = 0;
	int slack = 65536 - t->speed;
	if (!slack)
		t->debt = 0;
	while (n < max) {
		int avail = t->end - t->p;
		if (slack && t->debt >= AUDIO_TSM_JUMP << 16 && avail >= AUDIO_TSM_FADE
		    && max - n >= AUDIO_TSM_FADE) {
			int j = find_jump(t, t->debt >> 16);
			if (j > 0) {
				// Crossfade from the input at p to the input j frames back.
				const int16_t *x = t->buf + t->p * 2, *y = x - j * 2;
				for (int i = 0; i < AUDIO_TSM_FADE; i++) {
					int w = (i + 1) * 32768 / (AUDIO_TSM_FADE + 1);
					for (int c = 0; c < 2; c++) {
						int a = x[i * 2 + c], b = y[i * 2 + c];
						out[(n + i) * 2 + c] = a + (((b - a) * w + 16384) >> 15);
					}
				}
				n += AUDIO_TSM_FADE;
				t->p += AUDIO_TSM_FADE - j;
				t->debt += AUDIO_TSM_FADE * slack - (j << 16);
				t->jumps++;
				continue;
			}
			t->debt = 0;   // no history yet: start counting again
		}
		// Copy input unchanged up to the next jump, keeping a crossfade's
		// worth of input back while stretching.
		int k = slack ? avail - AUDIO_TSM_FADE : avail;
		if (slack) {
			int until = ((AUDIO_TSM_JUMP << 16) - t->debt + slack - 1) / slack;
			if (until < 1) until = 1;
			if (k > until) k = until;
		}
		if (k > (int)(max - n)) k = max - n;
		if (k <= 0)
			break;
		memcpy(out + n * 2, t->buf + t->p * 2, k * 4);
		n += k;
		t->p += k;
		t->debt += k * slack;
	}
	return n;
}
