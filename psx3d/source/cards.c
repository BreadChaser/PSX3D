// Memory cards on the SD card (harsh review C1). Card 1 is normally the
// core's SAVE_RAM, kept as <game>.srm; card 2 (and card 1 "per disc" or
// "shared") are files the core names. All are written crash-safe with a
// backup (safefile.h) only when they changed, read from their best good copy
// (exact size, "MC" header), and a card that could not be written stays due:
// it is tried again every CARD_RETRY_MS and the player is told.
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "psx3d.h"
#include "clip.h"

// What a good memory card / state file is (safefile.h): its exact size and
// the header the core looks at first. That is all a "good" backup means.
static bool card_head_ok(const void *head)
{
	const u8 *h = head;
	return h[0] == 'M' && h[1] == 'C';
}

static safe_format_t card_format(size_t size)
{
	return (safe_format_t){ size, 2, card_head_ok };
}

#define CARD_RETRY_MS (30 * 1000)   // a failed memory card write is tried again

// A memory card file into data: its best good copy, repaired when it came
// from the backup. Damaged without a good copy: data keeps the empty card the
// core made, and the file stays (as .bad once the card is written).
typedef enum { CARD_OK, CARD_UNSAVED, CARD_UNREAD } card_load_t;
static card_load_t card_load(const char *path, void *data, size_t size, const char *label)
{
	safe_format_t fmt = card_format(size);
	safe_info_t info;
	void *card = safe_load(path, &fmt, 0, &info);
	if (!card) {
		if (info.failed) {   // never write over a card we could not read
			log_printf("%s %s: no memory to read it, not saving it this time\n", label, path);
			emu_osd("Not enough memory to load the memory card");
			return CARD_UNREAD;
		}
		if (info.rejected) {
			log_printf("%s %s damaged, no backup: started with an empty card\n", label, path);
			emu_osd("Memory card file damaged: started with an empty card");
		}
		return CARD_OK;
	}
	memcpy(data, card, size);
	free(card);
	if (info.from == SAFE_CURRENT) {
		log_printf("loaded %s %s\n", label, path);
		return CARD_OK;
	}
	log_printf("%s %s damaged: loaded the %s\n", label, path, info.from == SAFE_BACKUP ? "backup" : "unfinished copy");
	if (safe_repair(path, &fmt, data, info.from)) {
		emu_osd("Save was damaged: restored the backup");
		return CARD_OK;
	}
	log_printf("%s: could not repair %s\n", label, path);
	emu_osd("Save was damaged: loaded the backup, could not fix the file");
	return CARD_UNSAVED;
}

static bool card_write(const char *path, const void *data, size_t size, const char *label)
{
	safe_format_t fmt = card_format(size);
	if (safe_write(path, &fmt, data, true)) {
		log_printf("saved %s %s\n", label, path);
		return true;
	}
	log_printf("%s: could not write %s\n", label, path);
	emu_osd("Could not save the memory card. Is the SD card full?");
	return false;
}

// memory card 1 lives in the core's SAVE_RAM; we persist it as <game>.srm
static u8 *srm_shadow;        // what the SD card holds
static bool srm_dirty;        // a write failed: the card still needs writing
static bool srm_locked;       // could not be read: never written over
static u64 srm_retry_at;
static char srm_path[PATH_LEN];

static void srm_load(void)
{
	void *mem = retro_get_memory_data(RETRO_MEMORY_SAVE_RAM);
	size_t size = retro_get_memory_size(RETRO_MEMORY_SAVE_RAM);

	free(srm_shadow);
	srm_shadow = NULL;
	srm_dirty = srm_locked = false;
	srm_retry_at = 0;
	if (!mem || !size)
		return;
	card_load_t r = card_load(srm_path, mem, size, "memory card");
	srm_locked = r == CARD_UNREAD;
	srm_dirty = r == CARD_UNSAVED;   // never shadowed: written again soon
	srm_shadow = malloc(size);
	if (srm_shadow)
		memcpy(srm_shadow, mem, size);
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
	if (!card_write(srm_path, mem, size, "memory card")) {
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
	u64 retry_at;
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
	card_load_t r = card_load(c->path, c->data, CORE_CARD_SIZE, mcd == 1 ? "memory card 1" : "memory card 2");
	c->locked = r == CARD_UNREAD;
	c->dirty = r == CARD_UNSAVED;
	c->shadow = malloc(CORE_CARD_SIZE);
	if (c->shadow)
		memcpy(c->shadow, c->data, CORE_CARD_SIZE);
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
		if (card_write(c->path, c->data, CORE_CARD_SIZE, i ? "memory card 2" : "memory card 1")) {
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
	srm_load();
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

