// Host test of box art matching (psx3d/source/boxart.c, run for real): serials,
// disc images in every sector layout, title keys, the shipped name table, and
// the memory of missing pictures. argv[1]: the romfs covers folder.
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "../../source/boxart.h"

static int checks;
#define CHECK(x) do { checks++; if (!(x)) { fprintf(stderr, "FAIL %s:%d %s\n", __FILE__, __LINE__, #x); exit(1); } } while (0)
#define STR(a, b) do { checks++; if (strcmp((a), (b))) { fprintf(stderr, "FAIL %s:%d \"%s\" != \"%s\"\n", __FILE__, __LINE__, (a), (b)); exit(1); } } while (0)

static char names[512], idx[512];

static void put32(uint8_t *p, uint32_t v) { p[0] = v; p[1] = v >> 8; p[2] = v >> 16; p[3] = v >> 24; }

// a disc with one file SYSTEM.CNF in the root directory
static void write_image(const char *path, int ss, int off, const char *cnf, bool with_cnf)
{
	FILE *f = fopen(path, "wb");
	CHECK(f);
	for (int lba = 0; lba < 24; lba++) {
		uint8_t sec[2352] = { 0 };
		uint8_t *d = sec + (ss == 2048 ? 0 : off);
		if (ss == 2352) {
			memset(sec + 1, 0xff, 10);
			sec[15] = off == 16 ? 1 : 2;
		}
		if (lba == 16) {
			d[0] = 1;
			memcpy(d + 1, "CD001", 5);
			d[156] = 34;
			put32(d + 158, 18);
			put32(d + 166, 2048);
		} else if (lba == 18 && with_cnf) {
			const char *name = "SYSTEM.CNF;1";
			uint8_t *r = d;
			// another file first, so the scan has to walk
			r[0] = 40; put32(r + 2, 21); put32(r + 10, 5); r[32] = 7; memcpy(r + 33, "README;", 7);
			r += 40;
			r[0] = 46; put32(r + 2, 20); put32(r + 10, strlen(cnf)); r[32] = strlen(name); memcpy(r + 33, name, strlen(name));
		} else if (lba == 20) {
			memcpy(d, cnf, strlen(cnf));
		}
		size_t n = ss;
		CHECK(fwrite(sec, 1, n, f) == n);
	}
	fclose(f);
}

static void test_serials(void)
{
	char s[10];
	CHECK(boxart_serial_norm("SLUS_006.94;1", s)); STR(s, "SLUS00694");
	CHECK(boxart_serial_norm("slus-00694", s)); STR(s, "SLUS00694");
	CHECK(boxart_serial_norm("SCES-00311", s)); STR(s, "SCES00311");
	CHECK(boxart_serial_norm("SLPS 01204", s)); STR(s, "SLPS01204");
	CHECK(boxart_serial_norm("SLUSP012.06;1", s)); STR(s, "SLUS01206");   // Dragon Warrior VII's odd boot file name
	CHECK(!boxart_serial_norm("SLUSQ012.06", s));
	CHECK(!boxart_serial_norm("Crash", s));
	CHECK(!boxart_serial_norm("", s));
	CHECK(boxart_serial_norm("DTLS2170", s)); STR(s, "DTLS2170");   // demo discs have 4 digits
	CHECK(!boxart_serial_norm("SLUS_00", s));          // too short
	CHECK(!boxart_serial_norm("SLUS_0069477", s));     // too long
	CHECK(!boxart_serial_norm("TOOLONG_00694", s));

	CHECK(boxart_serial_from_cnf("BOOT = cdrom:\\SLUS_006.94;1\r\nTCB = 4\r\nEVENT = 10\r\nSTACK = 801FFF00\r\n", s));
	STR(s, "SLUS00694");
	CHECK(boxart_serial_from_cnf("TCB = 4\nBOOT=cdrom:SCUS_949.00;1\n", s)); STR(s, "SCUS94900");
	CHECK(boxart_serial_from_cnf("BOOT = cdrom0:\\SCES_003.11\n", s)); STR(s, "SCES00311");
	CHECK(boxart_serial_from_cnf("boot = cdrom:/SLPS_012.04;1", s)); STR(s, "SLPS01204");
	CHECK(!boxart_serial_from_cnf("BOOT2 = cdrom0:\\SLUS_006.94;1\n", s));
	CHECK(!boxart_serial_from_cnf("TCB = 4\n", s));
	CHECK(!boxart_serial_from_cnf("BOOT = cdrom:\\PSX.EXE;1\n", s));
}

static void test_images(const char *dir)
{
	static const struct { int ss, off; } layouts[] = { { 2048, 0 }, { 2352, 16 }, { 2352, 24 }, { 2336, 8 } };
	char path[600], serial[10];
	const char *cnf = "BOOT = cdrom:\\SLUS_006.94;1\r\nTCB = 4\r\n";
	for (int i = 0; i < 4; i++) {
		snprintf(path, sizeof(path), "%s/disc%d.bin", dir, i);
		write_image(path, layouts[i].ss, layouts[i].off, cnf, true);
		CHECK(boxart_disc_serial(path, serial));
		STR(serial, "SLUS00694");
		// the same through a cue sheet in the same folder, with a quoted and a bare name
		char cue[600];
		snprintf(cue, sizeof(cue), "%s/disc%d.cue", dir, i);
		FILE *f = fopen(cue, "w");
		CHECK(f);
		fprintf(f, "FILE \"disc%d.bin\" BINARY\n  TRACK 01 MODE2/2352\n    INDEX 01 00:00:00\n", i);
		fclose(f);
		serial[0] = 0;
		CHECK(boxart_disc_serial(cue, serial));
		STR(serial, "SLUS00694");
	}
	snprintf(path, sizeof(path), "%s/nocnf.bin", dir);
	write_image(path, 2352, 16, "", false);
	CHECK(!boxart_disc_serial(path, serial));
	snprintf(path, sizeof(path), "%s/exe.bin", dir);
	write_image(path, 2048, 0, "BOOT = cdrom:\\PSX.EXE;1\n", true);
	CHECK(!boxart_disc_serial(path, serial));
	snprintf(path, sizeof(path), "%s/junk.bin", dir);
	FILE *f = fopen(path, "wb");
	for (int i = 0; i < 60000; i++) fputc(i * 7, f);
	fclose(f);
	CHECK(!boxart_disc_serial(path, serial));
	CHECK(!boxart_disc_serial("/nonexistent/game.bin", serial));
	CHECK(!boxart_disc_serial("/nonexistent/game.cue", serial));
	CHECK(!boxart_disc_serial("game.chd", serial));    // not readable here: matched by name instead
	CHECK(!boxart_disc_serial("game.pbp", serial));
	char cue[600];
	snprintf(cue, sizeof(cue), "%s/empty.cue", dir);
	f = fopen(cue, "w"); fputs("REM nothing\n", f); fclose(f);
	CHECK(!boxart_disc_serial(cue, serial));
}

static void test_keys(void)
{
	char k[256];
	boxart_title_key("Crash Bandicoot (USA) (Rev 1)", k, sizeof(k)); STR(k, "crashbandicoot");
	boxart_title_key("Legend of Dragoon, The (USA) (Disc 1)", k, sizeof(k)); STR(k, "legendofdragoon");
	boxart_title_key("The Legend of Dragoon", k, sizeof(k)); STR(k, "legendofdragoon");
	boxart_title_key("Metal Gear Solid [!] (Europe)", k, sizeof(k)); STR(k, "metalgearsolid");
	boxart_title_key("Spyro 2 - Ripto's Rage! (USA)", k, sizeof(k)); STR(k, "spyro2riptosrage");
	boxart_title_key("An American Tail", k, sizeof(k)); STR(k, "americantail");
	boxart_title_key("Theme Park (USA)", k, sizeof(k)); STR(k, "themepark");    // "the" only as a whole word
	boxart_title_key("", k, sizeof(k)); STR(k, "");
	boxart_sanitize("Chrono Cross: Disc 1 / ?", k, sizeof(k)); STR(k, "Chrono Cross_ Disc 1 _ _");
}

static void test_table(void)
{
	// the shipped table is sorted and every serial points at a line of the name list
	FILE *f = fopen(idx, "rb");
	CHECK(f);
	char rec[19] = "", prev[10] = "";
	int count = 0;
	while (fread(rec, 1, 18, f) == 18) {
		CHECK(rec[9] == ' ' && rec[17] == '\n');
		CHECK(count == 0 || memcmp(prev, rec, 9) < 0);
		memcpy(prev, rec, 9);
		count++;
	}
	fclose(f);
	CHECK(count > 9000);
	f = fopen(names, "rb");
	CHECK(f);
	char line[512], last[512] = "";
	int n = 0;
	while (fgets(line, sizeof(line), f)) {
		line[strcspn(line, "\n")] = 0;
		CHECK(strcmp(last, line) < 0);   // bytewise sorted: the binary search relies on it
		CHECK(boxart_name_known(names, line));
		strcpy(last, line);
		n++;
	}
	fclose(f);
	CHECK(n > 9000);
	CHECK(!boxart_name_known(names, "Crash Bandicoot (Mars)"));
	CHECK(!boxart_name_known(names, ""));
	CHECK(!boxart_name_known(names, "Crash Bandicoot (USA"));
	CHECK(!boxart_name_known("/nonexistent", "Crash Bandicoot (USA)"));
}

static int cands(const char *key, const char *serial, char out[][BOXART_NAME])
{
	return boxart_candidates(names, idx, key, serial, out, BOXART_MAX_CANDIDATES);
}

static void test_candidates(void)
{
	char c[BOXART_MAX_CANDIDATES][BOXART_NAME];
	// the file name is a redump name: that picture, nothing else is asked for
	CHECK(cands("Crash Bandicoot (USA)", NULL, c) == 1); STR(c[0], "Crash Bandicoot (USA)");
	CHECK(cands("Crash Bandicoot (USA)", "SLUS-99999", c) == 1); STR(c[0], "Crash Bandicoot (USA)");
	// whatever the file is called, the serial finds it
	CHECK(cands("Crash", "SCUS94900", c) >= 1); STR(c[0], "Crash Bandicoot (USA)");
	CHECK(cands("my crash backup", "SCUS_949.00", c) >= 1); STR(c[0], "Crash Bandicoot (USA)");
	// a serial in the file name is enough, e.g. SCUS-94900.chd
	CHECK(cands("SCUS-94900", NULL, c) >= 1); STR(c[0], "Crash Bandicoot (USA)");
	CHECK(cands("Crash Bandicoot [SCUS-94900]", NULL, c) >= 1); STR(c[0], "Crash Bandicoot (USA)");
	// no serial: the same title, region picked like the file's tags say
	CHECK(cands("Crash Bandicoot", NULL, c) >= 1); STR(c[0], "Crash Bandicoot (USA)");
	CHECK(cands("Crash Bandicoot (USA) (Rev 9)", NULL, c) >= 1); STR(c[0], "Crash Bandicoot (USA)");
	CHECK(cands("crash bandicoot (europe) [hack]", NULL, c) >= 1);
	CHECK(strstr(c[0], "Crash Bandicoot") && strstr(c[0], "Europe"));
	CHECK(cands("Final Fantasy VII (USA) (Disc 2) (Rev 7)", NULL, c) >= 1);
	STR(c[0], "Final Fantasy VII (USA) (Disc 2)");
	CHECK(cands("Final Fantasy VII", NULL, c) >= 1); STR(c[0], "Final Fantasy VII (USA)");   // the table has a box for the whole game
	CHECK(cands("Legend of Dragoon, The", NULL, c) >= 1);
	CHECK(!strncmp(c[0], "Legend of Dragoon, The (USA)", 28));
	CHECK(cands("The Legend of Dragoon (USA) (Disc 1)", NULL, c) >= 1);
	CHECK(!strncmp(c[0], "Legend of Dragoon, The (USA)", 28));
	// a name the table does not know is still asked for as it is (the table can be older)
	CHECK(cands("Totally Unknown Homebrew", NULL, c) == 1); STR(c[0], "Totally Unknown Homebrew");
	CHECK(cands("X", NULL, c) == 1); STR(c[0], "X");
	// characters the server replaces
	CHECK(cands("Weird: Name?", "", c) == 1); STR(c[0], "Weird_ Name_");
	// never more than asked, no duplicates
	char two[2][BOXART_NAME];
	int n = boxart_candidates(names, idx, "Crash", "SCUS94900", two, 2);
	CHECK(n >= 1 && n <= 2);
	n = cands("Crash Bandicoot", "SCUS94900", c);
	for (int i = 0; i < n; i++)
		for (int j = i + 1; j < n; j++)
			CHECK(strcmp(c[i], c[j]));
}

static void test_misses(const char *dir)
{
	char state[600];
	snprintf(state, sizeof(state), "%s/state.txt", dir);
	boxart_misses_load(state, "v1");
	CHECK(!boxart_miss_known("Crash", 1000));
	boxart_miss_add("Crash", 1000);
	boxart_miss_add("Odd\tKey (USA)", 5000);
	CHECK(boxart_miss_known("Crash", 1000));
	CHECK(boxart_miss_known("Crash", 1000 + BOXART_MISS_RETRY_MS - 1));
	CHECK(!boxart_miss_known("Crash", 1000 + BOXART_MISS_RETRY_MS));   // asked again after 14 days
	CHECK(!boxart_miss_known("Crash", 999));                           // a clock set back: not trusted
	CHECK(!boxart_miss_known("Other", 1000));
	boxart_user_set("Mine", 1234, 99);
	boxart_user_set("Mine", 1234, 99);
	CHECK(boxart_misses_save(state, "v1"));
	boxart_misses_free();
	// remembered across a restart
	boxart_misses_load(state, "v1");
	CHECK(boxart_miss_known("Crash", 2000));
	CHECK(boxart_miss_known("Odd\tKey (USA)", 5000));
	long sz, mt;
	CHECK(boxart_user_get("Mine", &sz, &mt) && sz == 1234 && mt == 99);
	CHECK(!boxart_user_get("Crash", &sz, &mt));
	// a rescan asks again; the player's own pictures stay marked
	boxart_misses_clear();
	CHECK(!boxart_miss_known("Crash", 2000));
	CHECK(boxart_user_get("Mine", &sz, &mt));
	CHECK(boxart_misses_save(state, "v1"));
	boxart_misses_free();
	boxart_misses_load(state, "v1");
	CHECK(!boxart_miss_known("Crash", 2000));
	// another app version: misses are dropped (the table may know more now), own pictures stay
	boxart_miss_add("Crash", 3000);
	CHECK(boxart_misses_save(state, "v1"));
	boxart_misses_free();
	boxart_misses_load(state, "v2");
	CHECK(!boxart_miss_known("Crash", 3000));
	CHECK(boxart_user_get("Mine", &sz, &mt));
	CHECK(boxart_misses_save(state, "v2"));   // rewritten with the new stamp
	boxart_misses_free();
	boxart_misses_load(state, "v2");
	boxart_user_clear("Mine");
	CHECK(!boxart_user_get("Mine", &sz, &mt));
	boxart_user_clear("Mine");
	// nothing changed: no file write
	boxart_misses_free();
	boxart_misses_load(state, "v2");
	struct stat before, after;
	CHECK(!stat(state, &before));
	CHECK(boxart_misses_save(state, "v2"));
	CHECK(!stat(state, &after) && before.st_mtime == after.st_mtime && before.st_size == after.st_size);
	// a damaged state file costs nothing but the memory
	FILE *f = fopen(state, "w");
	fputs("#boxart v2\nM notanumber Key\nM 12\nU x y\nU 1 2\nGARBAGE\nM 7 Fine\n", f);
	fclose(f);
	boxart_misses_free();
	boxart_misses_load(state, "v2");
	CHECK(boxart_miss_known("Fine", 7));
	CHECK(!boxart_miss_known("Key", 7));
	boxart_misses_free();
	boxart_misses_load("/nonexistent/state.txt", "v2");
	CHECK(!boxart_miss_known("Fine", 7));
	boxart_misses_free();
}

int main(int argc, char **argv)
{
	if (argc < 2) {
		fprintf(stderr, "usage: check <romfs/covers>\n");
		return 2;
	}
	snprintf(names, sizeof(names), "%s/names.txt", argv[1]);
	snprintf(idx, sizeof(idx), "%s/serials.idx", argv[1]);

	// command line mode for the match rate report and the cross check:
	// lines "key<TAB>serial" on stdin -> "key<TAB>n<TAB>candidate..."
	if (argc > 2 && !strcmp(argv[2], "--candidates")) {
		char line[1024];
		while (fgets(line, sizeof(line), stdin)) {
			line[strcspn(line, "\r\n")] = 0;
			char *tab = strchr(line, '\t');
			if (tab) *tab = 0;
			char c[BOXART_MAX_CANDIDATES][BOXART_NAME];
			int n = cands(line, tab ? tab + 1 : NULL, c);
			printf("%s\t%d", line, n);
			for (int i = 0; i < n; i++)
				printf("\t%s", c[i]);
			printf("\n");
		}
		return 0;
	}
	if (argc > 3 && !strcmp(argv[2], "--serial")) {   // disc image -> serial
		char s[10];
		puts(boxart_disc_serial(argv[3], s) ? s : "-");
		return 0;
	}

	char dir[] = "boxart-test-XXXXXX";   // the runner starts us in its own temp folder
	CHECK(mkdtemp(dir));
	test_serials();
	test_images(dir);
	test_keys();
	test_table();
	test_candidates();
	test_misses(dir);
	char cmd[700];
	snprintf(cmd, sizeof(cmd), "rm -rf '%s'", dir);
	CHECK(!system(cmd));
	printf("PASS boxart: %d checks\n", checks);
	return 0;
}
