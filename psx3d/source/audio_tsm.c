#include <stdlib.h>
#include <string.h>
#include "audio_tsm.h"

#define DEC 8                                   // coarse search: every 8th frame
#define REF (AUDIO_TSM_FADE / DEC)              // 32 coarse reference samples
#define FINE 2                                  // fine search: every 2nd frame

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

static inline int mono(const int16_t *f) { return (f[0] + f[1]) >> 1; }

// Normalised match of a against b: corr * |corr| / energy(a).
static float score(int c, int e)
{
	return (float)c * (float)(c < 0 ? -c : c) / ((float)e + 1.0f);
}

// How far back to jump from p: about want frames, to where the following
// AUDIO_TSM_FADE frames look most like the ones at p. A coarse search on
// 8-frame averages, then a fine one on every 2nd frame around the winner.
// Mono samples are scaled down so the 32-bit sums cannot overflow.
static int find_jump(const audio_tsm *t, int want)
{
	int lo = want - AUDIO_TSM_SEEK, hi = want + AUDIO_TSM_SEEK;
	if (lo < 64) lo = 64;
	if (hi > t->p) hi = t->p;
	if (hi < lo)
		return hi >= 64 ? hi : 0;
	const int16_t *b = t->buf;
	int16_t m[AUDIO_TSM_FADE + 2 * AUDIO_TSM_SEEK + 2 * DEC], ref[AUDIO_TSM_FADE];
	int cr[REF], cg[(2 * AUDIO_TSM_SEEK + AUDIO_TSM_FADE) / DEC + 2];
	// Region: candidate starts q = p - j for j in [lo, hi] cover [p - hi, p - lo + FADE).
	int base = t->p - hi, len = hi - lo + AUDIO_TSM_FADE;
	for (int i = 0; i < len; i++)
		m[i] = mono(b + (base + i) * 2) >> 4;
	for (int i = 0; i < AUDIO_TSM_FADE; i++)
		ref[i] = mono(b + (t->p + i) * 2) >> 4;
	for (int i = 0; i < REF; i++) {
		const int16_t *r = ref + i * DEC;
		cr[i] = (r[0] + r[1] + r[2] + r[3] + r[4] + r[5] + r[6] + r[7]) >> 4;
	}
	int ncand = (hi - lo) / DEC + 1, ng = ncand + REF - 1;
	for (int i = 0; i < ng; i++) {
		const int16_t *r = m + i * DEC;
		cg[i] = (r[0] + r[1] + r[2] + r[3] + r[4] + r[5] + r[6] + r[7]) >> 4;
	}
	int centre = (hi - want) / DEC, best_k = centre < ncand ? centre : ncand - 1;
	float best = -3.0e38f;
	int e = 0;
	for (int i = 0; i < REF - 1; i++)
		e += cg[i] * cg[i];
	for (int k = 0; k < ncand; k++) {
		e += cg[k + REF - 1] * cg[k + REF - 1];
		int c = 0;
		for (int i = 0; i < REF; i++)
			c += cr[i] * cg[k + i];
		float s = score(c, e);
		// Ties (silence) keep the candidate nearest the wanted jump.
		if (s > best || (s == best && abs(k - centre) < abs(best_k - centre))) {
			best = s;
			best_k = k;
		}
		e -= cg[k] * cg[k];
	}
	// Fine: every 2nd position around the coarse winner, then its neighbours.
	int j0 = hi - best_k * DEC, best_j = j0;
	float fbest = -3.0e38f;
	for (int pass = 0; pass < 2; pass++) {
		int from = pass ? best_j - 1 : j0 - DEC + 2, to = pass ? best_j + 1 : j0 + DEC - 2;
		for (int j = from; j <= to; j += 2) {
			if (j < lo || j > hi)
				continue;
			const int16_t *y = m + (hi - j);
			int c = 0, en = 0;
			for (int i = 0; i < AUDIO_TSM_FADE; i += FINE) {
				c += ref[i] * y[i];
				en += y[i] * y[i];
			}
			float sc = score(c, en);
			if (sc > fbest || (sc == fbest && j == j0)) {
				fbest = sc;
				best_j = j;
			}
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
					int w = (i + 1) * (16384 / AUDIO_TSM_FADE);   // Q14, ends at 1
					for (int c = 0; c < 2; c++) {
						int a = x[i * 2 + c], b = y[i * 2 + c];
						out[(n + i) * 2 + c] = a + (((b - a) * w + 8192) >> 14);
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
