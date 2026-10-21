// GPL-2.0-or-later. A deliberately small, fail-closed patch interpreter.
// Only curated data is read; arbitrary SD cheats never become screen settings.
#include "widescreen.h"
#include <stdio.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#define MAX_CODES 256
typedef struct { uint32_t op, value, original, last; bool touched; } code_t;
static code_t codes[MAX_CODES];
static unsigned count;
static bool enabled, allowed = true;
static int policy = -1;
static bool policy_override;
void ws_policy_override(bool on) { policy_override = on; }
static unsigned aspect_n = 5, aspect_d = 3;
static char identity[48];

static unsigned width(uint32_t op)
{
	switch (op >> 24) {
	case 0x30: return 1;
	case 0x80: case 0xA7: case 0xD0: return 2;
	case 0x90: case 0xA4: return 4;
	default: return 0;
	}
}
static uint32_t read_value(const uint8_t *ram, uint32_t a, unsigned n)
{
	uint32_t v = 0;
	for (unsigned i = 0; i < n; i++) v |= (uint32_t)ram[a + i] << (8 * i);
	return v;
}
static void write_value(uint8_t *ram, uint32_t a, unsigned n, uint32_t value)
{
	for (unsigned i = 0; i < n; i++) ram[a + i] = value >> (8 * i);
	ws_invalidate(a & ~3u, ((a & 3) + n + 3) & ~3u);
}
// Restore only words still owned by this bundle. An unloaded overlay must
// never have an old instruction written over its replacement.
static void restore(void)
{
	uint8_t *ram = ws_memory();
	if (!ram) return;
	bool live[MAX_CODES] = {0};
	unsigned skip_depth = 0;
	for (unsigned i = 0; i < count; i++) {
		code_t *c = &codes[i]; unsigned type = c->op >> 24;
		if (skip_depth) {
			if (type == 0xA4) skip_depth++;
			if (!c->op) skip_depth--;
			continue;
		}
		if (type == 0xA4) {
			if (read_value(ram, c->op & 0xFFFFFF, 4) != c->value) skip_depth = 1;
			continue;
		}
		if (type == 0xD0) {
			if (read_value(ram, c->op & 0xFFFFFF, 2) != c->value) i++;
			continue;
		}
		live[i] = true;
	}
	for (unsigned i = count; i-- > 0;) {
		code_t *c = &codes[i]; unsigned n = width(c->op);
		uint32_t a = c->op & 0xFFFFFF;
		if (live[i] && c->touched && read_value(ram, a, n) == c->last)
			write_value(ram, a, n, c->original);
		c->touched = false;
	}
}
void ws_unload(void)
{
	if (enabled) restore();
	enabled = false; count = 0; allowed = true; policy = -1; identity[0] = 0;
	aspect_n = 5; aspect_d = 3;
}
bool ws_load(const char *directory, const char *serial, uint32_t fingerprint, int console_policy)
{
	ws_unload();
	if (console_policy < WS_HIDDEN || console_policy > WS_TOO_SLOW) return false;
	// Serial strings originate in discs. Reject path separators and malformed IDs.
	if (!serial || strlen(serial) != 9) return false;
	char normalized[10];
	for (int i = 0; i < 9; i++) normalized[i] = toupper((unsigned char)serial[i]);
	normalized[9] = 0; serial = normalized;
	for (int i = 0; i < 9; i++)
		if (i < 4 ? (serial[i] < 'A' || serial[i] > 'Z') : (serial[i] < '0' || serial[i] > '9')) return false;
	char path[1024], line[1024];
	snprintf(path, sizeof(path), "%s/%s-%08X.ws", directory, serial, (unsigned)fingerprint);
	FILE *f = fopen(path, "r");
	if (!f) { snprintf(path, sizeof(path), "%s/%s.ws", directory, serial); f = fopen(path, "r"); }
	if (!f) return false;
	bool valid = true, header = false, aspect = false;
	unsigned depth = 0;
	while (fgets(line, sizeof(line), f)) {
		line[strcspn(line, "\r\n")] = 0;
		if (!line[0] || line[0] == '#' || line[0] == '[') continue;
		if (!strcmp(line, "PSX3D-WS-1")) { header = true; continue; }
		unsigned n, d; int used = 0;
		if (sscanf(line, "aspect=%u:%u%n", &n, &d, &used) == 2 && !line[used]) {
			if (n < 4 || n > 32 || d < 3 || d > 16) { valid = false; break; }
			aspect_n = n; aspect_d = d; aspect = true; continue;
		}
		unsigned op, val;
		used = 0;
		if (count == MAX_CODES || sscanf(line, "%8x %8x%n", &op, &val, &used) != 2 || line[used]) { valid = false; break; }
		if (op == 0 && val == 0xFFFF) {
			if (!depth) { valid = false; break; }
			depth--;
		} else {
			unsigned w = width(op); uint32_t a = op & 0xFFFFFF;
			if (!w || a > 0x200000 - w || (w == 2 && (a & 1)) || (w == 4 && (a & 3)) ||
			    ((op >> 24) != 0xA7 && w < 4 && val >= (1u << (8 * w)))) { valid = false; break; }
			if ((op >> 24) == 0xA4 && ++depth > 8) { valid = false; break; }
		}
		codes[count++] = (code_t){ .op = op, .value = val };
	}
	for (unsigned i = 0; i + 1 < count; i++)
		if ((codes[i].op >> 24) == 0xD0 &&
		    (codes[i + 1].op == 0 || (codes[i + 1].op >> 24) == 0xD0 || (codes[i + 1].op >> 24) == 0xA4)) valid = false;
	if (ferror(f)) valid = false;
	fclose(f);
	if (!valid || !header || !aspect || depth || !count || (codes[count - 1].op >> 24) == 0xD0) {
		ws_unload(); return false;
	}
	// The game's preset decides per console (docs/presets.md); bundles hold codes only.
	policy = console_policy;
	allowed = policy == WS_OFFERED || policy == WS_COSTLY;
	snprintf(identity, sizeof(identity), "%s-%08X", serial, (unsigned)fingerprint);
	return true;
}
bool ws_enable(bool on)
{
	on = on && count && (allowed || policy_override);
	if (enabled && !on) restore();
	enabled = on;
	return count && (allowed || policy_override);
}
bool ws_available(void) { return allowed || (count && policy_override); }
int ws_console_policy(void) { return count ? policy : -1; }
const char *ws_identity(void) { return identity[0] ? identity : "gte"; }
float ws_aspect(void) { return enabled ? (float)aspect_n / aspect_d : 5.0f / 3.0f; }
void ws_frame(void)
{
	if (!enabled) return;
	uint8_t *ram = ws_memory();
	if (!ram) return;
	unsigned skip_depth = 0;
	for (unsigned i = 0; i < count; i++) {
		code_t *c = &codes[i]; unsigned type = c->op >> 24, n = width(c->op);
		uint32_t a = c->op & 0xFFFFFF;
		if (skip_depth) {
			if (type == 0xA4) skip_depth++;
			if (!c->op) skip_depth--;
			continue;
		}
		if (!c->op) continue;
		uint32_t old = read_value(ram, a, n), v = c->value;
		if (type == 0xA4) { if (old != v) skip_depth = 1; continue; }
		if (type == 0xD0) { if (old != v) i++; continue; }
		if (type == 0xA7) {
			// A state may already contain the replacement instruction. Its
			// declared original makes rollback deterministic in that case too.
			if (old != (v >> 16) && old != (v & 0xFFFF)) continue;
			c->original = v >> 16; c->last = v & 0xFFFF; c->touched = true;
			v &= 0xFFFF;
		}
		if (old == v) continue;
		// Refresh the rollback value after an overlay reload or a game write.
		if (type != 0xA7 && (!c->touched || old != c->last)) c->original = old;
		c->last = v; c->touched = true;
		write_value(ram, a, n, v);
	}
}

// Save states include patched RAM, but never the host's ownership journal.
// Shipped writes use A7 with revision-specific originals; rebuild ownership
// from those values before restoring a state into a non-widescreen session.
void ws_state_loaded(void)
{
	uint8_t *ram = ws_memory();
	for (unsigned i = 0; i < count; i++) {
		code_t *c = &codes[i];
		c->touched = false;
		if (ram && (c->op >> 24) == 0xA7 &&
		    read_value(ram, c->op & 0xFFFFFF, 2) == (c->value & 0xFFFF)) {
			c->original = c->value >> 16; c->last = c->value & 0xFFFF; c->touched = true;
		}
	}
	if (enabled) ws_frame();
	else restore();
}
