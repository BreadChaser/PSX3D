// Frozen copy of v0.4.1 psx3d/source/game_presets.h (main 78592e1) and its
// game_capabilities() logic: the reference for tests/presets/identical.c.
// Do not edit; the data now lives in psx3d/romfs/presets/*.ini.
typedef struct {
	int fast_forward, rewind, touch_controls;
	int needs_l2r2, needs_l3r3, needs_right_stick;
	int touch_bindings[6];
	int frameskip;
	int widescreen; // 0 inherits, 1 supported, -1 unsupported
} legacy_options_t;
typedef struct { const char *serial; legacy_options_t old3ds, new3ds; } legacy_preset_t;
#undef GAME_PRESET_SAME
#define GAME_PRESET_SAME(disc, ...) { .serial = disc, .old3ds = __VA_ARGS__, .new3ds = __VA_ARGS__ }
#define LEGACY_AUTO 0 /* TOUCH_CONTROLS_AUTO */
#define INHERIT_SLOTS { -2, -2, -2, -2, -2, -2 }
#define CLICKS(disc) { .serial = disc, \
 .old3ds = { 0, 0, -1, -1, -1, -1, INHERIT_SLOTS, -1, 0 }, \
 .new3ds = { -1, -1, LEGACY_AUTO, 0, 1, 0, INHERIT_SLOTS, -1, 0 } }
#define FLAT2D(disc) GAME_PRESET_SAME(disc, { -1, -1, -1, -1, -1, -1, INHERIT_SLOTS, -1, -1 })
#define SOTN(disc) GAME_PRESET_SAME(disc, { -1, -1, -1, -1, -1, -1, INHERIT_SLOTS, -1, -1 })
static const legacy_preset_t legacy_presets[] = {
	// SotN: GPU rendering verified in the USA prologue fight.
	// GPU is used on both models.
	// Regional identities: libretro-database metadat/redump/Sony - PlayStation.dat;
	// All verified regional releases share the same capabilities.
	SOTN("SLUS00067"), SOTN("SLES00524"), SOTN("SCPS45196"),
	SOTN("SLPM86023"), SOTN("SLPM86073"), SOTN("SLPM87328"),
	// 2D-only presentations in the audited USA/Japan library: widening the
	// GTE cannot reveal extra scenery. Stretch remains available.
	FLAT2D("SLUS00553"), FLAT2D("SLUS00027"), FLAT2D("SLUS01384"),
	FLAT2D("SLPS02430"), FLAT2D("SLUS00879"), FLAT2D("SLUS00900"),
	FLAT2D("SLUS01363"), FLAT2D("SLUS01360"), FLAT2D("SLUS01541"),
	FLAT2D("SLUS01013"), FLAT2D("SLUS00628"), FLAT2D("SLUS00899"),
	FLAT2D("SLUS01071"), FLAT2D("SLUS01239"), FLAT2D("SLUS01240"),
	FLAT2D("SLUS00793"), FLAT2D("SLUS00453"), FLAT2D("SLUS00561"),
	FLAT2D("SLUS01334"), FLAT2D("SLUS01395"), FLAT2D("SLUS00190"),
	FLAT2D("SLUS00710"), FLAT2D("SLUS00731"), FLAT2D("SLUS00005"),
	FLAT2D("SLUS00041"), FLAT2D("SLUS00197"), FLAT2D("SLUS00258"),
	FLAT2D("SLUS00821"), FLAT2D("SLUS00423"), FLAT2D("SLUS00584"),
	FLAT2D("SLUS00418"),
	// Ape Escape / Saru! Get You!: HIGH, stick clicks confirmed by the user,
	// the US manual (crouch/crawl) and PSX DataCenter (L3 dive, R3 water net).
	CLICKS("SCUS94423"), // US
	CLICKS("SCES01564"), CLICKS("SCES02028"), CLICKS("SCES02029"), CLICKS("SCES02030"), CLICKS("SCES02031"), // PAL EN/FR/DE/IT/ES
	CLICKS("SCPS10091"), CLICKS("SCPS45411"), CLICKS("SCPS91196"), CLICKS("SCPS91331"), // JP/Asia/reissues
	// Omega Boost: LOW, user-supplied demo-disc forum report; retail use
	// unconfirmed. Same confidence applies to all listed regional editions.
	CLICKS("SCUS94449"), CLICKS("SCES02120"), CLICKS("SCPS10020"), CLICKS("SCPS45399"),
	// Twisted Metal 4: LOW, same demo-disc report; only a US serial verified.
	CLICKS("SCUS94560"),
	// Tomb Raider: The Last Revelation: LOW, same demo-disc report; regional
	// retail stick-click behavior not independently verified.
	CLICKS("SLUS00885"), CLICKS("SLES02238"), CLICKS("SLES02239"), CLICKS("SLES02240"), CLICKS("SLES02241"), CLICKS("SLES02242"),
	CLICKS("SLPS02803"), CLICKS("SLPM86896"),
	// Ace Combat 3: Electrosphere: LOW, same demo-disc report. JP is a
	// different two-disc edition; its controls need retail verification too.
	CLICKS("SLUS00972"), CLICKS("SCES02066"), CLICKS("SCPS45397"), CLICKS("SCPS45398"),
	CLICKS("SLPS02020"), CLICKS("SLPS02021"), CLICKS("SLPS91214"), CLICKS("SLPS91215"),
	// Supercross Circuit: LOW, same demo-disc report; only US verified.
	CLICKS("SCUS94453"),
};
#undef GAME_PRESET_SAME

// v0.4.1 presets.c game_capabilities() for one table entry (NULL = uncurated).
static legacy_options_t legacy_capabilities(const legacy_preset_t *preset, int new3ds)
{
	legacy_options_t result = {
		.frameskip = 1, .fast_forward = 1, .rewind = new3ds, .touch_controls = LEGACY_AUTO,
		.needs_l2r2 = !new3ds, .needs_l3r3 = 2, .needs_right_stick = new3ds ? 0 : 2,
		.touch_bindings = {-2, -2, -2, -2, -2, -2}, .widescreen = 1,
	};
	if (preset) {
		legacy_options_t p = new3ds ? preset->new3ds : preset->old3ds;
#define RESOLVE(field) if (p.field >= 0) result.field = p.field
		RESOLVE(frameskip); RESOLVE(fast_forward); RESOLVE(rewind); RESOLVE(touch_controls);
		RESOLVE(needs_l2r2); RESOLVE(needs_l3r3); RESOLVE(needs_right_stick);
#undef RESOLVE
		if (p.widescreen) result.widescreen = p.widescreen > 0;
		for (int i = 0; i < 6; i++)
			if (p.touch_bindings[i] != -2) result.touch_bindings[i] = p.touch_bindings[i];
	}
	if (!new3ds) result.rewind = 0;
	return result;
}
