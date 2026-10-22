// Box art matching: which picture on the thumbnail server belongs to a disc,
// whatever its file is called. Pure C, no 3DS calls (host tests run it).
//
// The server names pictures after redump names ("Crash Bandicoot (USA)").
// romfs:/covers/names.txt lists every picture it has and romfs:/covers/serials.idx
// maps a disc serial to one of them (tools/boxart/make_table.py builds both).
// A game's candidates, best first:
//   1. its own file name, when the server has exactly that picture
//   2. the picture of the serial read from the disc ("BOOT = cdrom:\SLUS_006.94;1")
//   3. the picture whose title matches once region, revision and disc tags,
//      punctuation, case and "The" are ignored (region, then disc, then revision
//      picked like the player's tags say)
// With none of these the plain file name is still tried once (the table can
// be older than the server).
//
// Pictures the server does not have are remembered in misses.txt with the time,
// so they are not asked for again at every start.
#ifndef BOXART_H
#define BOXART_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define BOXART_NAME 256
#define BOXART_MAX_CANDIDATES 3
// A picture the server did not have is asked for again after this long (or at once on a rescan)
#define BOXART_MISS_RETRY_MS (14ull * 24 * 60 * 60 * 1000)

// "SLUS_006.94;1", "slus-00694" -> "SLUS00694". false: not a disc serial.
bool boxart_serial_norm(const char *in, char out[10]);
// The serial named by a SYSTEM.CNF (text, NUL terminated)
bool boxart_serial_from_cnf(const char *cnf, char out[10]);
// The serial of a disc image (.cue, .bin, .img, .iso, .mdf; not .chd or .pbp). false: unknown.
bool boxart_disc_serial(const char *path, char out[10]);

// The words of a title: lower case letters and digits outside (...) and [...],
// without the articles "the", "a", "an", run together.
void boxart_title_key(const char *name, char *out, size_t len);
// libretro-thumbnails stores names with these characters replaced by '_'
void boxart_sanitize(const char *name, char *out, size_t len);

// Does the server have exactly this picture (sanitized name, no .png)?
bool boxart_name_known(const char *names_path, const char *key);
// Candidate names (sanitized, no .png), best first. serial may be NULL or "".
int boxart_candidates(const char *names_path, const char *idx_path, const char *key, const char *serial,
                      char out[][BOXART_NAME], int max);

// What is remembered between starts (one small text file, written crash-safe):
// pictures the server did not have, and covers made from the player's own PNG.
// stamp: the version of the app. Misses written by another version are dropped
// (the table or the server may have changed); the player's own pictures stay.
// now_ms: any steady clock in milliseconds.
void boxart_misses_load(const char *path, const char *stamp);
bool boxart_miss_known(const char *key, uint64_t now_ms);
void boxart_miss_add(const char *key, uint64_t now_ms);
void boxart_misses_clear(void);   // a rescan: ask for every missing picture again
bool boxart_misses_save(const char *path, const char *stamp);   // only when something changed
void boxart_misses_free(void);

// The player's own picture covers/<file name>.png beats any download. The
// cover cache made from it is marked with the PNG's size and time: a changed
// or replaced PNG is picked up, and a removed one hands the game back to the
// download.
bool boxart_user_get(const char *key, long *size, long *mtime);
void boxart_user_set(const char *key, long size, long mtime);
void boxart_user_clear(const char *key);
#endif
