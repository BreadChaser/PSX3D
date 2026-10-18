// Curated per-console profiles, normalized disc serials (not filenames).
// Fields -1 inherit defaults; touch_bindings use -2 for inheritance, -1 Hide.
// Existing evidence covers stick clicks only;
// other Old 3DS touch requirements remain at the full safe default.
#define INHERIT_SLOTS { -2, -2, -2, -2, -2, -2 }
#define INHERIT_OPTIONS { -1, -1, -1, -1, -1, -1, PRESET_RENDERER_INHERIT, INHERIT_SLOTS }
// GAME_PRESET_SAME("ABCD12345", { ff, rw, policy, l2r2, l3r3, stick, renderer, INHERIT_SLOTS })
// is shorthand for equal Old/New options (technical limits still apply).
#define CLICKS(disc) { .serial = disc, \
 .old3ds = { 0, 0, -1, -1, -1, -1, PRESET_RENDERER_INHERIT, INHERIT_SLOTS }, \
 .new3ds = { -1, -1, TOUCH_CONTROLS_AUTO, 0, 1, 0, PRESET_RENDERER_INHERIT, INHERIT_SLOTS } }
// Future TESTED Old FF example (not enabled):
// { "ABCD12345", { 1, -1, -1, -1, -1, -1, PRESET_RENDERER_INHERIT, INHERIT_SLOTS }, INHERIT_OPTIONS },
// Curated renderer example, enable ONLY after both accuracy and speed checks:
// { .serial = "ABCD12345", .old3ds = INHERIT_OPTIONS,
//   .new3ds = { -1, -1, -1, -1, -1, -1, PRESET_RENDERER_GPU, INHERIT_SLOTS } },
// DEFAULT uses Old GPU / New Software unless overridden by the user.
// Sources and regional coverage: docs/bottom-screen/README.md.
// Confidence below describes L3/R3 use, not serial identity. All IDs checked
// against PlayStation DataCenter. Add a source before extending this list.
#define SOTN(disc) GAME_PRESET_SAME(disc, { -1, -1, -1, -1, -1, -1, PRESET_RENDERER_SOFTWARE, INHERIT_SLOTS })
static const game_preset_t game_presets[] = {
	// SotN: round-two GPU approaches software parity in the USA fight.
	// Keep software for v0.3.0: it remains the proven picture on both models.
	// Regional identities: libretro-database metadat/redump/Sony - PlayStation.dat;
	// conservative software default applies to all verified regional releases.
	SOTN("SLUS00067"), SOTN("SLES00524"), SOTN("SCPS45196"),
	SOTN("SLPM86023"), SOTN("SLPM86073"), SOTN("SLPM87328"),
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

#undef SOTN
#undef CLICKS
#undef INHERIT_OPTIONS
#undef INHERIT_SLOTS
