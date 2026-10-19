// Curated per-console profiles, normalized disc serials (not filenames).
// Fields -1 inherit defaults; touch_bindings use -2 for inheritance, -1 Hide.
// Existing evidence covers stick clicks only;
// other Old 3DS touch requirements remain at the full safe default.
#define INHERIT_SLOTS { -2, -2, -2, -2, -2, -2 }
#define INHERIT_OPTIONS { -1, -1, -1, -1, -1, -1, INHERIT_SLOTS, -1, 0 }
// GAME_PRESET_SAME("ABCD12345", { ff, rw, policy, l2r2, l3r3, stick, INHERIT_SLOTS, -1, 0 })
// is shorthand for equal Old/New options (technical limits still apply).
#define CLICKS(disc) { .serial = disc, \
 .old3ds = { 0, 0, -1, -1, -1, -1, INHERIT_SLOTS, -1, 0 }, \
 .new3ds = { -1, -1, TOUCH_CONTROLS_AUTO, 0, 1, 0, INHERIT_SLOTS, -1, 0 } }
// Future TESTED Old FF example (not enabled):
// { "ABCD12345", { 1, -1, -1, -1, -1, -1, INHERIT_SLOTS, -1, 0 }, INHERIT_OPTIONS },
// Sources and regional coverage: docs/bottom-screen/README.md.
// Confidence below describes L3/R3 use, not serial identity. All IDs checked
// against PlayStation DataCenter. Add a source before extending this list.
#define FLAT2D(disc) GAME_PRESET_SAME(disc, { -1, -1, -1, -1, -1, -1, INHERIT_SLOTS, -1, -1 })
#define SOTN(disc) GAME_PRESET_SAME(disc, { -1, -1, -1, -1, -1, -1, INHERIT_SLOTS, -1, -1 })
static const game_preset_t game_presets[] = {
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

#undef FLAT2D
#undef SOTN
#undef CLICKS
#undef INHERIT_OPTIONS
#undef INHERIT_SLOTS
