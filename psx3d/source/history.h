// Save backups (owner's decision 2026-10-22, design devtools/ui-mock/
// SAVE-BACKUP-DESIGN.md "Recommended"): a ring of the last HIST_RING
// automatic saves per game, so a misclick (Start game at "Continue?", a
// wrong Load, saving over or deleting a slot, Reset) never destroys
// progress, plus copies of each memory card (the card when a session
// started, the first card of each day, a pinned copy before the game
// formats or empties it).
//
// <root>/<group>/<seq>-<kind><ext>   e.g. backups/Crash Bandicoot/00000012-start.sta
// plus <entry>.thm for a state's thumbnail. <group> is the game's save name,
// or a shared card file's name. Entries are self-describing (one file each,
// put in place by one rename or a crash-safe write), so there is no index
// that could go stale. States carry their own checksum (statefile.h), cards
// the PS1 checksums.
#ifndef HISTORY_H
#define HISTORY_H
#include <stdbool.h>
#include <stddef.h>
#include <time.h>

typedef enum {
	// states: the ring
	HIST_TIMER,        // an older Continue auto-save taken while playing
	HIST_EXIT,         // an older Continue auto-save taken on leaving (exit, HOME, sleep)
	HIST_LOAD,         // the game before a state was loaded
	HIST_SAVE,         // a slot's old save, before it was saved over
	HIST_RESET,        // the game before Reset game
	HIST_START,        // the Continue point when the player chose Start game
	HIST_DELETE,       // a slot the player deleted
	HIST_RESTORE,      // the game before an Undo / a restore from the ring
	// memory cards
	HIST_CARD_SESSION, // the card when the game started (newest only)
	HIST_CARD_DAY,     // the first card of a day (HIST_CARD_DAYS days)
	HIST_CARD_PIN,     // before the game formatted / emptied it, or before a put-back
	HIST_KINDS
} hist_kind_t;

#define HIST_RING 10              // automatic saves per game
#define HIST_CARD_DAYS 7
#define HIST_CARD_PINS 10         // per card file
#ifndef HIST_BUDGET
#define HIST_BUDGET (300u << 20)  // all games' ring entries together, bytes
#endif
#define HIST_IDLE_DAYS 30         // over the budget: games not played for this long ...
#define HIST_IDLE_KEEP 2          // ... keep only their newest few first

typedef struct {
	unsigned seq;
	hist_kind_t kind;
	char ext[16];             // ".sta", ".st3", ".srm", ".mcd"...
	time_t mtime;             // when that version was written
	long size;
	bool thumb;               // <path>.thm exists
	char path[320];           // <root>/<game>/<seq>-<kind><ext>
} hist_entry_t;

static inline bool hist_is_card(hist_kind_t k) { return k >= HIST_CARD_SESSION && k < HIST_KINDS; }

// Where the backups live (default PSX3D_DIR "/backups"; tests set another)
void history_set_root(const char *root);
// Move an existing file (and its thumbnail thm, may be NULL) into the
// backups as the group's newest entry. Never copies. false: nothing moved.
// *out (may be NULL): the entry.
bool history_adopt(const char *group, hist_kind_t kind, const char *ext, const char *file, const char *thm,
	hist_entry_t *out);
// The file name the next entry gets (the folder is created); write it
// crash-safe (safe_write), then call history_added().
bool history_next(const char *group, hist_kind_t kind, const char *ext, char *path, size_t len);
void history_added(const char *group, const char *path);
// Write data as a new entry (crash-safe, no thumbnail)
bool history_add(const char *group, hist_kind_t kind, const char *ext, const void *data, size_t size);
// The group's entries, newest first (at most max); returns the count.
// cards: true for memory card copies, false for the ring.
int history_list(const char *group, bool cards, hist_entry_t *out, int max);
// The entry written at that moment (an Undo target), or false
bool history_find(const char *group, time_t mtime, long size, hist_entry_t *out);
// Put an entry back as <file> (its thumbnail as <file>.thm): a rename
bool history_take(const hist_entry_t *e, const char *file);
const char *history_kind_name(hist_kind_t kind);
#endif
