// Memory cards on the SD card (harsh review C1, review 3 F1b/F1e). Card 1 is
// normally the core's SAVE_RAM, kept as <game>.srm; card 2 (and card 1 "per
// disc" or "shared") are files the core names. All are written crash-safe
// with a backup (safefile.h) only when they changed, read from their best
// good copy (exact size, "MC" header, PS1 checksums), and a card that could
// not be written stays due: it is tried again every CARD_RETRY_MS and the
// player is told. Copies go to the save backups (history.h): the card when
// the game started, the first card of each day, and a pinned copy before
// the game formats or empties it. A card that could not be read at all is
// reported before the game runs (cards_trouble).
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include "psx3d.h"
#include "clip.h"
#include "history.h"

// What a memory card file is (safefile.h): its exact size and the header the
// core looks at first ...
static bool card_head_ok(const void *head)
{
	const u8 *h = head;
	return h[0] == 'M' && h[1] == 'C';
}

// ... and, to be a good copy, the checksums the PS1 BIOS (and the HLE BIOS)
// write: byte 127 of the header frame and of the 15 directory frames is the
// XOR of the other 127. A card with a good header and a garbage directory is
// "weak": loaded only without a good copy, and never pushes a good backup out.
static bool card_deep_ok(const void *data, size_t size)
{
	const u8 *c = data;
	if (size < 16 * 128)
		return false;
	for (int f = 0; f < 16; f++) {
		u8 x = 0;
		for (int i = 0; i < 127; i++)
			x ^= c[f * 128 + i];
		if (x != c[f * 128 + 127])
			return false;
	}
	return true;
}

static safe_format_t card_format(size_t size)
{
	return (safe_format_t){ size, 2, card_head_ok, card_deep_ok, NULL, NULL };
}

// backups: <dir>/<group><ext> keeps its copies under <group>
typedef struct {
	char group[PATH_LEN];
	char ext[16];
} card_hist_t;

static void card_hist_init(card_hist_t *h, const char *path)
{
	const char *base = strrchr(path, '/');
	base = base ? base + 1 : path;
	snprintf(h->group, sizeof(h->group), "%s", base);
	char *dot = strrchr(h->group, '.');
	h->ext[0] = 0;
	if (dot && strlen(dot) < sizeof(h->ext)) {
		snprintf(h->ext, sizeof(h->ext), "%s", dot);
		*dot = 0;
	}
}

// saves on a card: directory frames whose block is the first of a save
int card_saves(const void *data, size_t size)
{
	const u8 *c = data;
	int n = 0;
	for (int f = 1; f < 16 && (size_t)(f + 1) * 128 <= size; f++)
		n += (c[f * 128] & 0xf) == 1 && (c[f * 128] & 0xf0) == 0x50;
	return n;
}

// a game started: copy the card as loaded (and the first one of the day)
static void card_session_copy(const card_hist_t *h, const void *data, size_t size)
{
	enum { MAX = 2 * (1 + HIST_CARD_DAYS + HIST_CARD_PINS) };
	if (clip_replay_session || !h->ext[0])
		return;
	hist_entry_t *list = malloc(MAX * sizeof(*list));
	history_add(h->group, HIST_CARD_SESSION, h->ext, data, size);
	long today_n = (long)(time(NULL) / 86400);   // the 3DS clock is local time
	bool today = false;
	int n = list ? history_list(h->group, true, list, MAX) : 0;
	for (int i = 0; i < n; i++)
		if (list[i].kind == HIST_CARD_DAY && !strcmp(list[i].ext, h->ext)) {
			today = (long)(list[i].mtime / 86400) == today_n;
			break;   // the newest day copy decides
		}
	free(list);
	if (list && !today)
		history_add(h->group, HIST_CARD_DAY, h->ext, data, size);
}

// The game is about to write a card with far fewer saves than the SD card
// holds (formatted, or most saves deleted): pin the SD card's version.
static void card_pin_if_emptied(const card_hist_t *h, const void *old, const void *now, size_t size)
{
	int before = old ? card_saves(old, size) : 0, after = card_saves(now, size);
	if (clip_replay_session || !before || !h->ext[0] || !(after == 0 || (before - after >= 2 && after * 2 <= before)))
		return;
	log_printf("memory card %s%s: %d saves -> %d, pinned a copy\n", h->group, h->ext, before, after);
	if (history_add(h->group, HIST_CARD_PIN, h->ext, old, size))
		emu_osd_alert("The game emptied the memory card: a copy of the old card is in Backups");
}

#define CARD_RETRY_MS (30 * 1000)   // a failed memory card write is tried again

// A memory card file into data: its best good copy, repaired when it came
// from the backup. Damaged without a good copy (CARD_DAMAGED), or no memory
// to read it (CARD_UNREAD): data keeps the empty card the core made, the card
// is locked (never written over) and cards_trouble() reports it.
typedef enum { CARD_OK, CARD_UNSAVED, CARD_UNREAD, CARD_DAMAGED } card_load_t;
static card_load_t card_load(const char *path, void *data, size_t size, const char *label)
{
	safe_format_t fmt = card_format(size);
	safe_info_t info;
	void *card = safe_load(path, &fmt, 0, &info);
	if (!card) {
		if (info.failed) {   // never write over a card we could not read
			log_printf("%s %s: no memory to read it\n", label, path);
			return CARD_UNREAD;
		}
		if (info.rejected) {
			log_printf("%s %s damaged, no good copy\n", label, path);
			return CARD_DAMAGED;
		}
		return CARD_OK;
	}
	memcpy(data, card, size);
	free(card);
	if (info.weak) {
		log_printf("%s %s fails its checksums and has no good copy: loaded as it is\n", label, path);
		emu_osd_alert("Memory card file looks damaged and has no backup: loaded it as it is");
	}
	if (info.from == SAFE_CURRENT) {
		log_printf("loaded %s %s\n", label, path);
		return CARD_OK;
	}
	log_printf("%s %s damaged: loaded the %s\n", label, path, info.from == SAFE_BACKUP ? "backup" : "unfinished copy");
	if (safe_repair(path, &fmt, data, info.from)) {
		emu_osd_alert("Memory card was damaged: restored the backup");
		return CARD_OK;
	}
	log_printf("%s: could not repair %s\n", label, path);
	emu_osd_alert("Memory card was damaged: loaded the backup, could not fix the file");
	return CARD_UNSAVED;
}

static bool card_write(const char *path, const void *data, const void *shadow, size_t size, const char *label,
	card_hist_t *h)
{
	safe_format_t fmt = card_format(size);
	card_pin_if_emptied(h, shadow, data, size);
	if (safe_write(path, &fmt, data, true)) {
		log_printf("saved %s %s\n", label, path);
		return true;
	}
	log_printf("%s: could not write %s\n", label, path);
	emu_osd_alert("Could not save the memory card. Is the SD card full?");
	return false;
}

static bool size_of_path(const char *path)
{
	struct stat st;
	char bak[PATH_LEN + 8];
	snprintf(bak, sizeof(bak), "%s.bak", path);
	return stat(path, &st) == 0 || stat(bak, &st) == 0;
}

static card_trouble_t trouble_of(card_load_t r)
{
	return r == CARD_UNREAD ? CARD_TROUBLE_MEMORY : r == CARD_DAMAGED ? CARD_TROUBLE_DAMAGED : CARD_TROUBLE_NONE;
}

// memory card 1 lives in the core's SAVE_RAM; we persist it as <game>.srm
static u8 *srm_shadow;        // what the SD card holds
static bool srm_dirty;        // a write failed: the card still needs writing
static bool srm_locked;       // could not be read: never written over
static card_trouble_t srm_trouble;
static u64 srm_retry_at;
static char srm_path[PATH_LEN];
static card_hist_t srm_hist;

static void srm_load(void)
{
	void *mem = retro_get_memory_data(RETRO_MEMORY_SAVE_RAM);
	size_t size = retro_get_memory_size(RETRO_MEMORY_SAVE_RAM);

	free(srm_shadow);
	srm_shadow = NULL;
	srm_dirty = srm_locked = false;
	srm_trouble = CARD_TROUBLE_NONE;
	srm_retry_at = 0;
	if (!mem || !size)
		return;
	bool had = size_of_path(srm_path);
	card_load_t r = card_load(srm_path, mem, size, "memory card");
	srm_trouble = trouble_of(r);
	srm_locked = srm_trouble != CARD_TROUBLE_NONE;
	srm_dirty = r == CARD_UNSAVED;   // never shadowed: written again soon
	srm_shadow = malloc(size);
	if (srm_shadow)
		memcpy(srm_shadow, mem, size);
	if (had && !srm_locked)
		card_session_copy(&srm_hist, mem, size);
}

// true when the card changed and was written
static bool srm_save(void)
{
	void *mem = retro_get_memory_data(RETRO_MEMORY_SAVE_RAM);
	size_t size = retro_get_memory_size(RETRO_MEMORY_SAVE_RAM);

	if (!mem || !size || !srm_path[0] || clip_replay_session || srm_locked)
		return false;
	if (!srm_dirty && srm_shadow && memcmp(srm_shadow, mem, size) == 0)
		return false;
	if (srm_dirty && osGetTime() < srm_retry_at)
		return false;
	if (!card_write(srm_path, mem, srm_shadow, size, "memory card", &srm_hist)) {
		srm_dirty = true;
		srm_retry_at = osGetTime() + CARD_RETRY_MS;
		return false;
	}
	srm_dirty = false;
	if (srm_shadow)
		memcpy(srm_shadow, mem, size);
	return true;
}

// Memory cards the core keeps in files of its own (card 2, card 1 "per disc"
// or "shared"): the core would write them in place, 128 bytes at a time, and
// read whatever is there. Instead PSX3D reads them here (best good copy, like
// <game>.srm), then takes the file name away from the core: it keeps the card
// in memory only, exactly as it does for card 1 normally, and PSX3D writes the
// card crash-safe with a backup when it changed. (The HLE BIOS's "format"
// then does nothing, as it already did for card 1.) Files with a header
// (DexDrive, VGS) stay with the core as before.
#define CORE_CARD_SIZE (128 * 1024)
extern char Mcd1Data[], Mcd2Data[];
void __real_LoadMcd(int mcd, char *str);
static struct {
	char path[PATH_LEN];
	char *data;
	u8 *shadow;         // what the SD card holds
	bool on, dirty, locked;
	card_trouble_t trouble;
	u64 retry_at;
	card_hist_t hist;
} core_card[2];

static long size_of(const char *path)
{
	struct stat st;
	return stat(path, &st) == 0 ? (long)st.st_size : -1;
}

void __wrap_LoadMcd(int mcd, char *str)
{
	typeof(core_card[0]) *c = mcd == 1 || mcd == 2 ? &core_card[mcd - 1] : NULL;
	long sz = str && *str ? size_of(str) : -1;
	if (c) {
		free(c->shadow);
		memset(c, 0, sizeof(*c));
	}
	if (!c || !str || !*str || !strcmp(str, "none") || strlen(str) >= sizeof(c->path) ||
	    sz == CORE_CARD_SIZE + 64 || sz == CORE_CARD_SIZE + 3904) {
		__real_LoadMcd(mcd, str);
		return;
	}
	snprintf(c->path, sizeof(c->path), "%s", str);
	c->data = mcd == 1 ? Mcd1Data : Mcd2Data;   // the core's empty card until loaded
	card_hist_init(&c->hist, c->path);
	card_load_t r = card_load(c->path, c->data, CORE_CARD_SIZE, mcd == 1 ? "memory card 1" : "memory card 2");
	c->trouble = trouble_of(r);
	c->locked = c->trouble != CARD_TROUBLE_NONE;
	c->dirty = r == CARD_UNSAVED;
	c->shadow = malloc(CORE_CARD_SIZE);
	if (c->shadow)
		memcpy(c->shadow, c->data, CORE_CARD_SIZE);
	if (sz >= 0 && !c->locked)
		card_session_copy(&c->hist, c->data, CORE_CARD_SIZE);
	c->on = true;
	str[0] = 0;               // the core's Config.McdN: memory only from now on
	__real_LoadMcd(mcd, str); // still marks the card as inserted and new
}

// write the core's cards that changed (now: also right after a failed write);
// true when one was written
static bool core_cards_save(bool now)
{
	bool saved = false;
	for (int i = 0; i < 2; i++) {
		typeof(core_card[0]) *c = &core_card[i];
		if (!c->on || c->locked || clip_replay_session)
			continue;
		if (!c->dirty && c->shadow && !memcmp(c->shadow, c->data, CORE_CARD_SIZE))
			continue;
		if (c->dirty && !now && osGetTime() < c->retry_at)
			continue;
		if (card_write(c->path, c->data, c->shadow, CORE_CARD_SIZE, i ? "memory card 2" : "memory card 1", &c->hist)) {
			c->dirty = false;
			if (c->shadow)
				memcpy(c->shadow, c->data, CORE_CARD_SIZE);
			saved = true;
		} else {
			c->dirty = true;
			c->retry_at = osGetTime() + CARD_RETRY_MS;
		}
	}
	return saved;
}

void cards_begin(const char *srm)
{
	snprintf(srm_path, sizeof(srm_path), "%s", srm);
	card_hist_init(&srm_hist, srm_path);
	srm_load();
}

card_trouble_t cards_trouble(char *label, size_t len)
{
	if (srm_trouble) {
		snprintf(label, len, "%s", srm_path);
		return srm_trouble;
	}
	for (int i = 0; i < 2; i++)
		if (core_card[i].on && core_card[i].trouble) {
			snprintf(label, len, "%s", core_card[i].path);
			return core_card[i].trouble;
		}
	return CARD_TROUBLE_NONE;
}

bool cards_retry(void)
{
	void *mem = retro_get_memory_data(RETRO_MEMORY_SAVE_RAM);
	size_t size = retro_get_memory_size(RETRO_MEMORY_SAVE_RAM);
	if (srm_trouble && mem && size) {
		card_load_t r = card_load(srm_path, mem, size, "memory card");
		srm_trouble = trouble_of(r);
		srm_locked = srm_trouble != CARD_TROUBLE_NONE;
		srm_dirty = r == CARD_UNSAVED;
		if (!srm_locked && srm_shadow)   // what the SD card holds now
			memcpy(srm_shadow, mem, size);
	}
	for (int i = 0; i < 2; i++) {
		typeof(core_card[0]) *c = &core_card[i];
		if (!c->on || !c->trouble)
			continue;
		card_load_t r = card_load(c->path, c->data, CORE_CARD_SIZE, i ? "memory card 2" : "memory card 1");
		c->trouble = trouble_of(r);
		c->locked = c->trouble != CARD_TROUBLE_NONE;
		c->dirty = r == CARD_UNSAVED;
		if (!c->locked && c->shadow)
			memcpy(c->shadow, c->data, CORE_CARD_SIZE);
	}
	char dummy[8];
	return cards_trouble(dummy, sizeof(dummy)) == CARD_TROUBLE_NONE;
}

// The player chose to play on. A damaged file: the game gets the empty card
// and its first save keeps the damaged file as .bad. No memory to read it:
// the card stays locked for this session (the file is never written over).
void cards_accept(void)
{
	if (srm_trouble == CARD_TROUBLE_DAMAGED)
		srm_locked = false;
	srm_trouble = CARD_TROUBLE_NONE;
	for (int i = 0; i < 2; i++) {
		if (core_card[i].trouble == CARD_TROUBLE_DAMAGED)
			core_card[i].locked = false;
		core_card[i].trouble = CARD_TROUBLE_NONE;
	}
}

bool cards_check(void)
{
	bool card = srm_save();
	return core_cards_save(false) || card;
}

// leaving the game, sleep, HOME: every card now, failed writes included
void cards_save_now(void)
{
	srm_retry_at = 0;
	srm_save();
	core_cards_save(true);
}

void cards_end(void)
{
	free(srm_shadow);
	srm_shadow = NULL;
	srm_path[0] = 0;
	for (int i = 0; i < 2; i++) {
		free(core_card[i].shadow);
		memset(&core_card[i], 0, sizeof(core_card[i]));
	}
}


// ---------------------------------------------------------------- backups UI

// The cards this game uses: 0 = card 1 (<game>.srm or the core's card 1),
// then the core's card 2. false: no such card.
bool cards_backup_group(int i, char *group, size_t glen, char *ext, size_t elen, const char **label)
{
	int k = 0;
	if (srm_path[0] && srm_shadow && !core_card[0].on) {
		if (k++ == i) {
			snprintf(group, glen, "%s", srm_hist.group);
			snprintf(ext, elen, "%s", srm_hist.ext);
			*label = "Memory card 1";
			return true;
		}
	}
	for (int c = 0; c < 2; c++)
		if (core_card[c].on && k++ == i) {
			snprintf(group, glen, "%s", core_card[c].hist.group);
			snprintf(ext, elen, "%s", core_card[c].hist.ext);
			*label = c ? "Memory card 2" : "Memory card 1";
			return true;
		}
	return false;
}

// saves on that card as the game has it now (-1: no such card)
int cards_saves_now(int i)
{
	int k = 0;
	if (srm_path[0] && srm_shadow && !core_card[0].on && k++ == i)
		return card_saves(retro_get_memory_data(RETRO_MEMORY_SAVE_RAM), retro_get_memory_size(RETRO_MEMORY_SAVE_RAM));
	for (int c = 0; c < 2; c++)
		if (core_card[c].on && k++ == i)
			return card_saves(core_card[c].data, CORE_CARD_SIZE);
	return -1;
}

// Put a card copy back: the card as it is now is pinned first, the copy is
// written to the card file and replaces the card in memory. The caller
// restarts the game (games keep their own view of the card's directory).
bool cards_put_back(const hist_entry_t *e)
{
	struct {
		const char *path, *label;
		void *mem;
		u8 **shadow;
		bool *dirty, *locked;
		card_hist_t *h;
		size_t size;
	} t = { 0 };
	if (!strcmp(e->ext, srm_hist.ext) && strstr(e->path, srm_hist.group) && srm_shadow) {
		t = (typeof(t)){ srm_path, "memory card", retro_get_memory_data(RETRO_MEMORY_SAVE_RAM), &srm_shadow,
			&srm_dirty, &srm_locked, &srm_hist, retro_get_memory_size(RETRO_MEMORY_SAVE_RAM) };
	}
	for (int c = 0; c < 2 && !t.path; c++)
		if (core_card[c].on && !strcmp(e->ext, core_card[c].hist.ext) && strstr(e->path, core_card[c].hist.group))
			t = (typeof(t)){ core_card[c].path, c ? "memory card 2" : "memory card 1", core_card[c].data,
				&core_card[c].shadow, &core_card[c].dirty, &core_card[c].locked, &core_card[c].hist, CORE_CARD_SIZE };
	if (!t.path || !t.mem || *t.locked || clip_replay_session)
		return false;
	safe_format_t fmt = { t.size, 2, card_head_ok, NULL, NULL, NULL };
	void *copy = safe_load(e->path, &fmt, 0, NULL);
	if (!copy)
		return false;
	history_add(t.h->group, HIST_CARD_PIN, t.h->ext, t.mem, t.size);   // the card as it is now
	safe_format_t cf = card_format(t.size);
	bool ok = safe_write(t.path, &cf, copy, true);
	if (ok) {
		memcpy(t.mem, copy, t.size);
		if (*t.shadow)
			memcpy(*t.shadow, copy, t.size);
		*t.dirty = false;
		log_printf("%s: put back %s\n", t.label, e->path);
	}
	free(copy);
	return ok;
}
