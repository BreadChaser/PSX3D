// Input: 3DS buttons sit in the same physical places as the PS1 face buttons
// (A=Circle, B=Cross, X=Triangle, Y=Square), so the mapping is 1:1.
// The circle pad doubles as the d-pad for digital games and is also the
// left analog stick; the New 3DS C-stick is the right stick.
#include "psx3d.h"
#include <math.h>
#include <string.h>
#include <stdio.h>

static void touch_update(void);
static int touch_default_binding(int slot);
static bool touch_button_visible(int i);
static bool touch_stick_active(void);
static void actions_update(void);
static bool touch_slot_held(int i);
static bool touch_map_enabled(int id);
static void touch_menu_update(void);
static int stick_x, stick_y;
static unsigned touch_revision;
static bool pad_analog;   // the game has the pad in analog mode
int padAnalogMode(unsigned int index);

static touchPosition touch;

static u32 held, down;
static u16 touch_mask(void);
static circlePosition cpad, cstick;

#define CPAD_DPAD_THRESHOLD 60

// remappable buttons (input_map_*); -1 = does nothing in the game
static const u32 remap_keys[INPUT_PHYSICAL_N] = { KEY_A, KEY_B, KEY_X, KEY_Y, KEY_L, KEY_R, KEY_ZL, KEY_ZR, KEY_START, KEY_SELECT };
static const char *const remap_names[INPUT_REMAP_N] = { "A", "B", "X", "Y", "L", "R", "ZL", "ZR", "START", "SELECT", "TOUCH_L2", "TOUCH_R2", "TOUCH_L3", "TOUCH_R3", "TOUCH_X1", "TOUCH_X2" };
static const int remap_default[INPUT_REMAP_N] = {
	RETRO_DEVICE_ID_JOYPAD_A, RETRO_DEVICE_ID_JOYPAD_B, RETRO_DEVICE_ID_JOYPAD_X, RETRO_DEVICE_ID_JOYPAD_Y,
	RETRO_DEVICE_ID_JOYPAD_L, RETRO_DEVICE_ID_JOYPAD_R, RETRO_DEVICE_ID_JOYPAD_L2, RETRO_DEVICE_ID_JOYPAD_R2,
	RETRO_DEVICE_ID_JOYPAD_START, RETRO_DEVICE_ID_JOYPAD_SELECT,
	RETRO_DEVICE_ID_JOYPAD_L2, RETRO_DEVICE_ID_JOYPAD_R2, RETRO_DEVICE_ID_JOYPAD_L3, RETRO_DEVICE_ID_JOYPAD_R3,
	INPUT_FF_TOGGLE, INPUT_REWIND_HOLD,
};
static int remap[INPUT_REMAP_N];
static bool remap_init;

// the PS1 buttons a 3DS button can be set to, in menu order
static const int ps1_ids[] = {
	RETRO_DEVICE_ID_JOYPAD_A, RETRO_DEVICE_ID_JOYPAD_B, RETRO_DEVICE_ID_JOYPAD_X, RETRO_DEVICE_ID_JOYPAD_Y,
	RETRO_DEVICE_ID_JOYPAD_L, RETRO_DEVICE_ID_JOYPAD_R, RETRO_DEVICE_ID_JOYPAD_L2, RETRO_DEVICE_ID_JOYPAD_R2,
	RETRO_DEVICE_ID_JOYPAD_L3, RETRO_DEVICE_ID_JOYPAD_R3, RETRO_DEVICE_ID_JOYPAD_START, RETRO_DEVICE_ID_JOYPAD_SELECT,
	RETRO_DEVICE_ID_JOYPAD_UP, RETRO_DEVICE_ID_JOYPAD_DOWN, RETRO_DEVICE_ID_JOYPAD_LEFT, RETRO_DEVICE_ID_JOYPAD_RIGHT,
	INPUT_FF_TOGGLE, INPUT_FF_HOLD, INPUT_REWIND_HOLD, -1,
};
#define N_PS1 (int)(sizeof(ps1_ids) / sizeof(ps1_ids[0]))

const char *input_ps1_name(int id)
{
	switch (id) {
	case RETRO_DEVICE_ID_JOYPAD_A: return "Circle";
	case RETRO_DEVICE_ID_JOYPAD_B: return "Cross";
	case RETRO_DEVICE_ID_JOYPAD_X: return "Triangle";
	case RETRO_DEVICE_ID_JOYPAD_Y: return "Square";
	case RETRO_DEVICE_ID_JOYPAD_L: return "L1";
	case RETRO_DEVICE_ID_JOYPAD_R: return "R1";
	case RETRO_DEVICE_ID_JOYPAD_L2: return "L2";
	case RETRO_DEVICE_ID_JOYPAD_R2: return "R2";
	case RETRO_DEVICE_ID_JOYPAD_L3: return "L3";
	case RETRO_DEVICE_ID_JOYPAD_R3: return "R3";
	case RETRO_DEVICE_ID_JOYPAD_START: return "Start";
	case RETRO_DEVICE_ID_JOYPAD_SELECT: return "Select";
	case RETRO_DEVICE_ID_JOYPAD_UP: return "Up";
	case RETRO_DEVICE_ID_JOYPAD_DOWN: return "Down";
	case RETRO_DEVICE_ID_JOYPAD_LEFT: return "Left";
	case RETRO_DEVICE_ID_JOYPAD_RIGHT: return "Right";
	case INPUT_FF_TOGGLE: return "Fast forward (toggle)";
	case INPUT_FF_HOLD: return "Fast forward (hold)";
	case INPUT_REWIND_HOLD: return "Rewind (hold)";
	}
	return "None";
}

static void remap_defaults(void)
{
	for (int i = 0; i < INPUT_REMAP_N; i++)
		remap[i] = i < INPUT_PHYSICAL_N ? remap_default[i] : INPUT_BIND_PRESET;
	remap_init = true;
	touch_revision++;
}

int input_map_factory(int i) { return i>=0 && i<INPUT_REMAP_N ? remap_default[i] : -1; }
const char *input_map_key_name(int i) { return i >= 0 && i < INPUT_REMAP_N ? remap_names[i] : ""; }
int input_map_raw(int i)
{
	if (!remap_init) remap_defaults();
	return i >= 0 && i < INPUT_REMAP_N ? remap[i] : -1;
}
int input_map_get(int i)
{
	int binding = input_map_raw(i);
	return i >= INPUT_PHYSICAL_N && binding == INPUT_BIND_PRESET ? touch_default_binding(i - INPUT_PHYSICAL_N)
																 : binding;
}

bool input_map_is_default(int i) { return i >= 0 && i < INPUT_REMAP_N && input_map_raw(i) == (i < INPUT_PHYSICAL_N ? remap_default[i] : INPUT_BIND_PRESET); }

void input_map_set(int i, int id)
{
	if (!remap_init) remap_defaults();
	if (i < 0 || i >= INPUT_REMAP_N)
		return;
	if (i >= INPUT_PHYSICAL_N && id == INPUT_BIND_PRESET) {
		remap[i] = id;
		touch_revision++;
		return;
	}
	for (int k = 0; k < N_PS1; k++)
		if (ps1_ids[k] == id) {
			remap[i] = id;
			touch_revision++;
			return;
		}
}

// step a button's PS1 assignment forward/back through the list
void input_map_cycle(int i, int dir)
{
	int cur = 0;
	for (int k = 0; k < N_PS1; k++)
		if (ps1_ids[k] == input_map_get(i))
			cur = k;
	do { cur = (cur + dir + N_PS1) % N_PS1; }
	while (ps1_ids[cur] >= INPUT_FF_TOGGLE && !touch_map_enabled(ps1_ids[cur]));
	input_map_set(i, ps1_ids[cur]);
}

void input_map_reset(void)
{
	remap_defaults();
	input_hotkey_set(0, 39); input_hotkey_set(1, 35);
}

static const struct { u32 key; unsigned id; } map[] = {
	{ KEY_DUP,    RETRO_DEVICE_ID_JOYPAD_UP },
	{ KEY_DDOWN,  RETRO_DEVICE_ID_JOYPAD_DOWN },
	{ KEY_DLEFT,  RETRO_DEVICE_ID_JOYPAD_LEFT },
	{ KEY_DRIGHT, RETRO_DEVICE_ID_JOYPAD_RIGHT },
};

// Config stores readable names; combo indices are private to the Controls UI.
static const u32 hotkey_combos[] = {
	0,
	KEY_A | KEY_B,
	KEY_A | KEY_X,
	KEY_A | KEY_Y,
	KEY_A | KEY_L,
	KEY_A | KEY_R,
	KEY_A | KEY_ZL,
	KEY_A | KEY_ZR,
	KEY_A | KEY_START,
	KEY_A | KEY_SELECT,
	KEY_B | KEY_X,
	KEY_B | KEY_Y,
	KEY_B | KEY_L,
	KEY_B | KEY_R,
	KEY_B | KEY_ZL,
	KEY_B | KEY_ZR,
	KEY_B | KEY_START,
	KEY_B | KEY_SELECT,
	KEY_X | KEY_Y,
	KEY_X | KEY_L,
	KEY_X | KEY_R,
	KEY_X | KEY_ZL,
	KEY_X | KEY_ZR,
	KEY_X | KEY_START,
	KEY_X | KEY_SELECT,
	KEY_Y | KEY_L,
	KEY_Y | KEY_R,
	KEY_Y | KEY_ZL,
	KEY_Y | KEY_ZR,
	KEY_Y | KEY_START,
	KEY_Y | KEY_SELECT,
	KEY_L | KEY_R,
	KEY_L | KEY_ZL,
	KEY_L | KEY_ZR,
	KEY_L | KEY_START,
	KEY_L | KEY_SELECT,
	KEY_R | KEY_ZL,
	KEY_R | KEY_ZR,
	KEY_R | KEY_START,
	KEY_R | KEY_SELECT,
	KEY_ZL | KEY_ZR,
	KEY_ZL | KEY_START,
	KEY_ZL | KEY_SELECT,
	KEY_ZR | KEY_START,
	KEY_ZR | KEY_SELECT,
	KEY_START | KEY_SELECT,
};
#define HOTKEY_COMBOS (int)(sizeof(hotkey_combos)/sizeof(hotkey_combos[0]))
static int hotkeys[INPUT_HOTKEY_N] = { 39, 35 }; // SELECT+R / SELECT+L
bool input_hotkey_is_default(int i) { return i >= 0 && i < INPUT_HOTKEY_N && hotkeys[i] == (i ? 35 : 39); }
static bool gameplay, ff_on, ff_hold, rw_hold;
static u32 action_blocked, previous_toggles, consumed_keys;

int input_hotkey_get(int i) { return i >= 0 && i < INPUT_HOTKEY_N ? hotkeys[i] : 0; }
void input_hotkey_set(int i, int combo)
{
	if (i >= 0 && i < INPUT_HOTKEY_N && combo >= 0 && combo < HOTKEY_COMBOS)
		hotkeys[i] = combo;
}
const char *input_hotkey_key_name(int i) { return i == 0 ? "hotkey_ff" : "hotkey_rewind"; }
const char *input_hotkey_combo_name(int combo)
{
	static char name[32];
	if (combo <= 0 || combo >= HOTKEY_COMBOS) return "off";
	int a = -1, b = -1;
	for (int i = 0; i < INPUT_PHYSICAL_N; i++)
		if (hotkey_combos[combo] & remap_keys[i]) { if (a < 0) a = i; else b = i; }
	// Put SELECT/START first, matching the customary default spelling.
	if (b >= 8) { int t = a; a = b; b = t; }
	snprintf(name, sizeof(name), "%s+%s", remap_names[a], remap_names[b]);
	return name;
}
void input_hotkey_load(int i, const char *name)
{
	char wanted[32];
	snprintf(wanted, sizeof(wanted), "%s", name);
	for (int c = 0; c < HOTKEY_COMBOS; c++)
		if (!strcmp(wanted, input_hotkey_combo_name(c))) { input_hotkey_set(i, c); return; }
}

void input_gameplay(bool enabled)
{
	gameplay = enabled;
	ff_on = ff_hold = rw_hold = false;
	previous_toggles = consumed_keys = 0;
	action_blocked = held; // menu/HOME gestures must release before firing actions
}

// Developer remote: one press for whichever menu polls next. Real buttons
// still work; gameplay never sees it, so the game and hotkeys stay local.
static u32 remote_key;
bool input_remote_key(const char *name)
{
	static const char *const names[] = {"a", "b", "x", "y", "up", "down", "left", "right", "l", "r", "select"};
	static const u32 keys[] = {KEY_A,	 KEY_B,	   KEY_X,	  KEY_Y, KEY_UP,
							   KEY_DOWN, KEY_LEFT, KEY_RIGHT, KEY_L, KEY_R, KEY_SELECT};
	if (gameplay || remote_key)
		return false;
	for (unsigned i = 0; i < sizeof(keys) / sizeof(keys[0]); i++)
		if (!strcmp(name, names[i])) {
			remote_key = keys[i];
			return true;
		}
	return false;
}

void input_poll(void)
{
	hidScanInput();
	held = hidKeysHeld();
	down = hidKeysDown() | (gameplay ? 0 : remote_key);
	remote_key = 0;
	if (gameplay && pad_analog != (bool)padAnalogMode(0)) {
		pad_analog = !pad_analog;   // the game switched the pad: show/hide stick buttons
		touch_revision++;
	}
	hidCircleRead(&cpad);
	irrstCstickRead(&cstick);
	hidTouchRead(&touch);
	touch_update();
	touch_menu_update();
	actions_update();
}

u32 input_keys_down(void) { return down; }
u32 input_keys_held(void) { return held; }

static void actions_update(void)
{
	if (!gameplay) return;
	game_preset_options_t caps = game_capabilities();
	if (!caps.fast_forward) ff_on = false;
	action_blocked &= held;
	consumed_keys = 0;
	u32 toggles = 0;
	ff_hold = rw_hold = false;
	for (int i = 0; i < INPUT_HOTKEY_N; i++) {
		u32 combo = (i ? caps.rewind : caps.fast_forward) ? hotkey_combos[hotkeys[i]] : 0;
		if (combo && !(combo & action_blocked) && (held & combo) == combo) {
			consumed_keys |= combo;
			if (i == 0) toggles |= 1u << INPUT_REMAP_N;
			else rw_hold = true;
		}
	}
	for (int i = 0; i < INPUT_REMAP_N; i++) {
		bool pressed = i < INPUT_PHYSICAL_N ?
			(held & ~action_blocked & ~consumed_keys & remap_keys[i]) != 0 :
			!(action_blocked & KEY_TOUCH) && touch_slot_held(i-INPUT_PHYSICAL_N);
		if (!pressed || !touch_map_enabled(input_map_get(i))) continue;
		switch (input_map_get(i)) {
		case INPUT_FF_TOGGLE: toggles |= 1u << i; break;
		case INPUT_FF_HOLD: ff_hold = true; break;
		case INPUT_REWIND_HOLD: rw_hold = true; break;
		}
	}
	if (toggles & ~previous_toggles) ff_on = !ff_on;
	previous_toggles = toggles;
	if (rw_hold) ff_on = false; // rewind takes priority over either FF mode
}

bool input_fast_forward(void) { return game_capabilities().fast_forward && gameplay && !rw_hold && (ff_on || ff_hold); }
void input_fast_forward_off(void) { ff_on = ff_hold = false; }
bool input_rewind(void) { return game_capabilities().rewind && gameplay && rw_hold; }

static u16 joypad_mask(void)
{
	u16 mask = 0;
	u32 game_held = held & ~consumed_keys;
	if (!remap_init)
		remap_defaults();
	for (int i = 0; i < INPUT_PHYSICAL_N; i++)
		if ((game_held & remap_keys[i]) && input_map_get(i) >= 0 && input_map_get(i) < 16)
			mask |= 1 << input_map_get(i);
	for (size_t i = 0; i < sizeof(map) / sizeof(map[0]); i++)
		if (game_held & map[i].key)
			mask |= 1 << map[i].id;

	mask |= touch_mask();

	// circle pad as d-pad (libctru's KEY_CPAD_* use a small threshold, use our own)
	if (cpad.dy >  CPAD_DPAD_THRESHOLD) mask |= 1 << RETRO_DEVICE_ID_JOYPAD_UP;
	if (cpad.dy < -CPAD_DPAD_THRESHOLD) mask |= 1 << RETRO_DEVICE_ID_JOYPAD_DOWN;
	if (cpad.dx < -CPAD_DPAD_THRESHOLD) mask |= 1 << RETRO_DEVICE_ID_JOYPAD_LEFT;
	if (cpad.dx >  CPAD_DPAD_THRESHOLD) mask |= 1 << RETRO_DEVICE_ID_JOYPAD_RIGHT;
	return mask;
}

static int16_t axis(int v)
{
	// 3DS pads report about +/-156; scale to the libretro +/-32767 range
	int r = v * 32767 / 150;
	if (r > 32767) r = 32767;
	if (r < -32768) r = -32768;
	return (int16_t)r;
}

int16_t input_state(unsigned port, unsigned device, unsigned index, unsigned id)
{
	if (port != 0)
		return 0;

	switch (device & RETRO_DEVICE_MASK) {
	case RETRO_DEVICE_JOYPAD: {
		u16 mask = joypad_mask();
		if (id == RETRO_DEVICE_ID_JOYPAD_MASK)
			return (int16_t)mask;
		return id < 16 ? (mask >> id) & 1 : 0;
	}
	case RETRO_DEVICE_ANALOG:
		if (index == RETRO_DEVICE_INDEX_ANALOG_LEFT)
			return id == RETRO_DEVICE_ID_ANALOG_X ? axis(cpad.dx) : axis(-cpad.dy);
		if (index == RETRO_DEVICE_INDEX_ANALOG_RIGHT) {
			if (!g_new3ds)
				return touch_stick_active() ? (id == RETRO_DEVICE_ID_ANALOG_X ? stick_x : stick_y) * 32767 / 31 : 0;
			return id == RETRO_DEVICE_ID_ANALOG_X ? axis(cstick.dx) : axis(-cstick.dy);
		}
		return 0;
	}
	return 0;
}

// ---------------------------------------------------------------- touch
// The resistive screen supports one contact. Capture a gesture at touchdown:
// dragging the stick never accidentally presses a neighbouring button.
typedef struct { int x, y, w, h; const char *slot; } tbutton_t;
static const tbutton_t tbuttons[] = {
	{ 92, 12, 105, 105, "L2" }, { 203, 12, 105, 105, "R2" },
	{ 92, 123, 105, 105, "L3" }, { 203, 123, 105, 105, "R3" },
	{ 12, 64, 56, 48, "X1" }, { 12, 128, 56, 48, "X2" },
	{ 0, 0, 82, 40, "MENU" },
};
// Indices match the Old 3DS remap slots; absent physical controls only.
static const tbutton_t new_tbuttons[] = {
	{ 0 }, { 0 },
	{ 208, 12, 100, 102, "L3" }, { 208, 126, 100, 102, "R3" },
	{ 0 }, { 0 },
	{ 0, 0, 82, 40, "MENU" },
};
#define TB_MENU INPUT_TOUCH_N
#define TB_STICK (TB_MENU + 1)
#define STICK_X 200
#define STICK_Y 120
static int touch_hit = -1;
static bool needed(int need) { return need == NEEDS_ANALOG ? pad_analog : need; }
static bool touch_blocked;
static bool menu_pending, stats_pending;

// Binding precedence: explicit global/game map, then preset/default bindings.
// Preset policy only supplies initial bindings; explicit slots always win.
static int touch_default_binding(int slot)
{
	game_preset_options_t c = game_capabilities();
	int mode = c.touch_controls;
	if (mode == TOUCH_CONTROLS_OFF)
		return INPUT_BIND_HIDE;
	if (mode == TOUCH_CONTROLS_ON)
		return remap_default[INPUT_PHYSICAL_N + slot];
	if (c.touch_bindings[slot] != INPUT_BIND_PRESET)
		return c.touch_bindings[slot];
	bool need = slot < 2 ? c.needs_l2r2 : slot < 4 ? needed(c.needs_l3r3) : slot == 4 ? c.fast_forward : c.rewind;
	return need ? remap_default[INPUT_PHYSICAL_N + slot] : INPUT_BIND_HIDE;
}
int input_stick_binding(void)
{
	if (g_new3ds)
		return 0;
	if (g_cfg.touch_stick >= 0)
		return g_cfg.touch_stick;
	game_preset_options_t c = game_capabilities();
	int mode = c.touch_controls;
	return mode == TOUCH_CONTROLS_ON || (mode != TOUCH_CONTROLS_OFF && needed(c.needs_right_stick));
}
void input_stick_set(int binding)
{
	if (binding < -1 || binding > 1)
		return;
	g_cfg.touch_stick = binding;
	input_touch_reset();
}
static unsigned touch_parts(void)
{
	static game_preset_options_t previous_caps;
	static unsigned revision, cached;
	static int model = -1, stick;
	static bool analog;
	game_preset_options_t caps = game_capabilities();
	if (model == g_new3ds && stick == g_cfg.touch_stick && revision == touch_revision && analog == pad_analog &&
		!memcmp(&previous_caps, &caps, sizeof(caps)))
		return cached;
	previous_caps = caps;
	model = g_new3ds;
	stick = g_cfg.touch_stick;
	analog = pad_analog;
	revision = touch_revision;
	unsigned parts = input_stick_binding() ? 1u << INPUT_TOUCH_N : 0;
	for (int i = 0; i < INPUT_TOUCH_N; i++) {
		if (g_new3ds && i < 2)
			continue;
		if (i == 4 && !caps.fast_forward)
			continue;
		if (i == 5 && !caps.rewind)
			continue;
		if (touch_map_enabled(input_map_get(INPUT_PHYSICAL_N + i)))
			parts |= 1u << i;
	}
	cached = parts;
	return cached;
}
static bool touch_stick_active(void)
{
	return !(gameplay && g_cfg.show_fps == 2) && input_stick_binding() != 0;
}
bool touch_buttons_active(void)
{
	return touch_parts() != 0;
}

// Called after per-game config is loaded, before the core loads the disc.
void input_touch_begin(void)
{
	game_presets_reset();
	pad_analog = false;
	int legacy = gamecfg_int("touch_controls", TOUCH_CONTROLS_AUTO);
	// Migrate the removed shortcut into explicit independent slot bindings.
	if (legacy == TOUCH_CONTROLS_OFF || legacy == TOUCH_CONTROLS_ON) {
		for (int i = INPUT_PHYSICAL_N; i < INPUT_REMAP_N; i++)
			if (legacy == TOUCH_CONTROLS_OFF || input_map_raw(i) == INPUT_BIND_PRESET)
				input_map_set(i, legacy == TOUCH_CONTROLS_OFF ? INPUT_BIND_HIDE : remap_default[i]);
		g_cfg.touch_stick = legacy == TOUCH_CONTROLS_ON;
	}
	input_touch_reset();
}

// CdromId is private in the core's static archive. misc.c:CheckCdrom logs it
// through the existing libretro callback on load and disc change. Consume that
// exact message; no filename guesses, pad-read heuristics or per-frame lookup.
void input_touch_disc_log(const char *message)
{
	if (strncmp(message, "CD-ROM ID: ", 11))
		return;
	game_preset_options_t before = game_capabilities();
	game_presets_disc_log(message);
	options_mark_updated(); // a curated profile can change the effective frameskip policy
	game_preset_options_t after = game_capabilities();
	if (memcmp(&before, &after, sizeof(before))) {
		input_gameplay(gameplay);
		input_touch_reset();
	}
}

static bool touch_button_visible(int i)
{
	return i == TB_MENU ? touch_buttons_active() : i >= 0 && i < INPUT_TOUCH_N && (touch_parts() & (1u << i));
}
static tbutton_t touch_button(int i)
{
	tbutton_t b = g_new3ds ? new_tbuttons[i] : tbuttons[i];
	bool left = g_new3ds && g_cfg.touch_click_left;
	if (g_new3ds && (i == 2 || i == 3)) {
		// New 3DS keeps the approved stacked edge column, not the Old cluster.
		if (left) b.x = 12;
		unsigned clicks = touch_parts() & 12;
		if (clicks == 4 || clicks == 8) { b.y = 12; b.h = 216; }
	} else if (!g_new3ds && i < 4) {
		// Tile the Old 3DS cluster footprint only.
		unsigned mask = touch_parts() & 15;
		int count = __builtin_popcount(mask), rank = 0;
		for (int k = 0; k < 4; k++)
			if ((mask & (1u << k)) && (((k & 1) < (i & 1)) || ((k & 1) == (i & 1) && k < i)))
				rank++;
		int x = 92;
		if (touch_stick_active()) {
			// Lone rows fill horizontally; two buttons sharing a row split vertically.
			bool column = count == 2 && (mask == 3 || mask == 12);
			if (count == 1) {
				b.x = x; b.y = 12; b.w = b.h = 216;
			} else if (column) {
				b.y = 12; b.h = 216;
			} else if (!(mask & (1u << (i ^ 1)))) {
				b.x = x; b.w = 216;
			}
		} else if (count == 1) {
			b.x = x;
			b.y = 12;
			b.w = b.h = 216;
		} else if (count == 2) {
			b.x = x + 111 * rank;
			b.y = 12;
			b.w = 105;
			b.h = 216;
		} else if (count == 3) {
			bool full_top = (mask & 3) == 3;
			bool wide = full_top ? i >= 2 : i < 2;
			b.x = x + (wide ? 0 : (i & 1) * 111);
			b.y = 12 + (i >= 2 ? 111 : 0);
			b.w = wide ? 216 : 105;
			b.h = 105;
			// Diagonal/column triples still have one complete row, so this is stable.
		} else {
			b.x = x + (i & 1) * 111;
			b.y = 12 + (i >= 2 ? 111 : 0);
			b.w = b.h = 105;
		}
	}
	if (i == 4 || i == 5) {
		b = tbuttons[i];
		if (left)
			b.x = 252;
		unsigned side = touch_parts() & 48;
		if (side == 16 || side == 32) {
			b.y = 64;
			b.h = 112;
		}
	}
	if (i == TB_MENU && left)
		b.x = 238;
	return b;
}

static bool touch_button_contains(int i, int x, int y)
{
	tbutton_t b = touch_button(i);
	if (!touch_button_visible(i) || x < b.x || x >= b.x+b.w || y < b.y || y >= b.y+b.h)
		return false;
	// The inside edge of each quadrant follows the same r=60 arc as its art.
	int dx = x-STICK_X, dy = y-STICK_Y;
	return !touch_stick_active() || i >= 4 || dx*dx + dy*dy >= 60*60;
}

void input_touch_reset(void)
{
	touch_hit = -1;
	stick_x = stick_y = 0;
	touch_blocked = true; // don't pass a menu gesture into the game
	menu_pending = stats_pending = false;
	touch_revision++;
}

// FPS uses the brand corner; Detailed retains its full-width footer.
static bool stats_contains(int x, int y)
{
	if (g_cfg.show_fps == 2) return y >= 218;
	int edge = g_new3ds && g_cfg.touch_click_left ? 246 : 12;
	return g_cfg.show_fps == 1 && x >= edge && x < edge+62 && y >= 184 && y < 224;
}

static void touch_update(void)
{
	stick_x = stick_y = 0;
	if (!(held & KEY_TOUCH)) {
		touch_hit = -1;
		touch_blocked = false;
		return;
	}
	if (gameplay && stats_contains(touch.px, touch.py)) {
		touch_hit = -1;
		if (!touch_blocked && (down & KEY_TOUCH)) stats_pending = true;
		return;
	}
	if (gameplay && g_cfg.show_fps == 2) {
		touch_hit = -1;
		if (!touch_blocked && (down & KEY_TOUCH) && touch.px >= 125 && touch.px < 195 && touch.py >= 184 && touch.py < 214)
			menu_pending = true;
		return;
	}
	if (!touch_buttons_active() || touch_blocked)
		return;
	if (down & KEY_TOUCH) {
		touch_hit = -1;
		for (int i = 0; i <= TB_MENU; i++) {
			if (touch_button_contains(i, touch.px, touch.py))
				touch_hit = i;
		}
		int x = (int)touch.px - STICK_X, y = (int)touch.py - STICK_Y;
		if (touch_stick_active() && x*x + y*y <= 50*50)
			touch_hit = TB_STICK;
	}
	if (touch_hit == TB_STICK && touch_stick_active()) {
		int x = (int)touch.px - STICK_X, y = (int)touch.py - STICK_Y;
		int len2 = x*x + y*y;
		if (len2 <= 4*4)
			return;
		if (len2 > 31*31) {
			float scale = 31.0f / sqrtf(len2);
			x = (int)(x * scale); y = (int)(y * scale);
		}
		stick_x = x; stick_y = y;
	}
}

static void touch_menu_update(void)
{
	if (gameplay && (g_cfg.show_fps == 2 || stats_contains(touch.px, touch.py))) return;
	if (!touch_blocked && (down & KEY_TOUCH) && (!touch_buttons_active() || touch_hit == TB_MENU))
		menu_pending = true;
}

bool input_stats_touched(void)
{
 bool pending = stats_pending;
 stats_pending = false;
 return pending;
}

bool menu_touched(void)
{
	// FF runs several retro frames/input polls before the outer menu check.
	// Keep a short MENU tap even if later polls see its release.
	bool pending = menu_pending;
	menu_pending = false;
	return pending;
}

static bool touch_slot_held(int i)
{
	return !(gameplay && g_cfg.show_fps == 2) && touch_buttons_active() && !touch_blocked && (held & KEY_TOUCH) &&
		touch_hit == i && touch_button_visible(i) && i >= 0 && i < TB_MENU && touch_map_enabled(input_map_get(INPUT_PHYSICAL_N+i));
}

static bool touch_map_enabled(int id)
{
	if (id < 0)
		return false;
	if (id == INPUT_FF_TOGGLE || id == INPUT_FF_HOLD)
		return game_capabilities().fast_forward;
	if (id == INPUT_REWIND_HOLD)
		return game_capabilities().rewind;
	return id < 16;
}

static u16 touch_mask(void)
{
	if ((gameplay && g_cfg.show_fps == 2) || !touch_buttons_active() || touch_hit < 0 || touch_hit >= TB_MENU)
		return 0;
	int id = input_map_get(INPUT_PHYSICAL_N + touch_hit);
	return id >= 0 && id < 16 ? 1 << id : 0;
}

bool input_touch_changed(void)
{
	static int last_hit = -2, last_x, last_y, last_stick, last_active, last_side;
	static bool last_ff_lit;
	static unsigned last_revision, last_parts;
	bool ff_lit = input_fast_forward();
	bool changed = last_hit != touch_hit || last_x != stick_x || last_y != stick_y ||
		last_parts != touch_parts() || last_active != touch_buttons_active() || last_stick != g_cfg.touch_stick ||
		last_side != g_cfg.touch_click_left || last_revision != touch_revision || last_ff_lit != ff_lit;
	last_ff_lit = ff_lit; last_parts = touch_parts();
	last_hit = touch_hit; last_x = stick_x; last_y = stick_y;
	last_active = touch_buttons_active();
	last_side = g_cfg.touch_click_left;
	last_stick = g_cfg.touch_stick; last_revision = touch_revision;
	return changed;
}

// Rows filter individual capabilities; persistent map slots never change indices.
// Special items: INPUT_REMAP_N = stick, +1/+2 = FF/rewind hotkeys.
static int control_item(int row)
{
	int n = 0;
	for (int i = 0; i < INPUT_REMAP_N + 3; i++) {
		bool visible = true;
		if (i >= INPUT_PHYSICAL_N && i < INPUT_REMAP_N) {
			int t = i - INPUT_PHYSICAL_N;
			if (g_new3ds)
				visible = t >= 2;
			if (t == 4)
				visible = game_capabilities().fast_forward;
			else if (t == 5)
				visible = game_capabilities().rewind;
		} else if (i == INPUT_REMAP_N)
			visible = !g_new3ds;
		else if (i == INPUT_REMAP_N + 1)
			visible = game_capabilities().fast_forward;
		else if (i == INPUT_REMAP_N + 2)
			visible = game_capabilities().rewind;
		if (visible && n++ == row)
			return i;
	}
	return -1;
}
int input_controls_count(void)
{
	int n = 0;
	while (control_item(n) >= 0)
		n++;
	return n;
}
int input_control_slot(int row)
{
	int i = control_item(row);
	return i >= 0 && i < INPUT_REMAP_N ? i : -1;
}
const char *input_controls_help(void)
{
	game_preset_options_t c = game_capabilities();
	if (c.rewind && c.fast_forward)
		return "Map buttons to PS1 controls, fast-forward toggle/hold, rewind hold or none. Change hotkey "
			   "pairs or choose off. Enable Rewind in Settings first. Hide removes a touch slot. Bind empty slots to show them.";
	if (c.rewind)
		return "Map buttons to PS1 controls, rewind hold or none. Change its hotkey pair or choose off. "
			   "Enable Rewind in Settings first. Hide removes a touch slot. Bind empty slots to show them.";
	if (c.fast_forward)
		return "Map buttons to PS1 controls, fast-forward toggle/hold or none. Change its hotkey pair or "
			   "choose off. Hide removes a touch slot. Bind empty slots to show them. The touch screen accepts one contact at a "
			   "time.";
	return "Map physical and touch buttons to PS1 controls or none. Hide removes a touch slot. Bind empty slots to show them. The "
		   "touch screen accepts one contact at a time. Touch controls can be changed in Settings.";
}
const char *input_control_label(int row, void *ud)
{
	(void)ud;
	static const char *const labels[] = {
		"Touch top left", "Touch top right", "Touch bottom left", "Touch bottom right", "Touch side 1", "Touch side 2"
	};
	int i = control_item(row);
	if (i < 0)
		return "";
	if (i == INPUT_REMAP_N)
		return "Touch right stick";
	if (i > INPUT_REMAP_N)
		return i == INPUT_REMAP_N + 1 ? "Hotkey: fast forward" : "Hotkey: rewind";
	if (g_new3ds && (i == INPUT_PHYSICAL_N+2 || i == INPUT_PHYSICAL_N+3))
		return i == INPUT_PHYSICAL_N+2 ? "Touch top" : "Touch bottom";
	return i >= INPUT_PHYSICAL_N ? labels[i - INPUT_PHYSICAL_N] : input_map_key_name(i);
}
const char *input_control_value(int row, void *ud)
{
	(void)ud;
	int i = control_item(row);
	if (i < 0)
		return "";
	if (i == INPUT_REMAP_N)
		return input_stick_binding() ? "Right stick" : "Hide";
	if (i > INPUT_REMAP_N) {
		const char *combo = input_hotkey_combo_name(input_hotkey_get(i - INPUT_REMAP_N - 1));
		return strcmp(combo, "off") ? combo : "Off";   // "off" is also the saved name
	}
	int id = input_map_get(i);
	if (i>=INPUT_PHYSICAL_N && !touch_map_enabled(id)) return "Hide";
	if (id >= INPUT_FF_TOGGLE && !touch_map_enabled(id)) return "None";
	if (id == INPUT_FF_TOGGLE)
		return "Fast fwd (toggle)";
	if (id == INPUT_FF_HOLD)
		return "Fast fwd (hold)";
	return input_ps1_name(id);
}
u32 input_control_color(int row)
{
	int i = control_item(row);
	if (i >= 0 && i < INPUT_REMAP_N) {
		int id = input_map_get(i);
		return ui_input_color(touch_map_enabled(id) ? id : INPUT_BIND_HIDE);
	}
	if (i == INPUT_REMAP_N && !input_stick_binding()) return ui_input_color(INPUT_BIND_HIDE);
	return ui_input_color(RETRO_DEVICE_ID_JOYPAD_L);
}

void input_control_cycle(int row, int dir)
{
	int i = control_item(row);
	if (i < 0)
		return;
	if (i > INPUT_REMAP_N) {
		int hk = i - INPUT_REMAP_N - 1;
		int combo = input_hotkey_get(hk);
		do {
			combo = (combo + dir + HOTKEY_COMBOS) % HOTKEY_COMBOS;
		} while (!g_new3ds && (hotkey_combos[combo] & (KEY_ZL | KEY_ZR)));
		input_hotkey_set(hk, combo);
	} else if (i == INPUT_REMAP_N)
		input_stick_set(!input_stick_binding());
	else
		input_map_cycle(i, dir);
}

// Approved controller artwork. Arc geometry is cached; no geometry
// or drawing work is done on frames where the cached bottom target is retained.
static u32 rgb(unsigned hex) { return C2D_Color32(hex>>16, hex>>8, hex, 255); }

// Precompose 40% opacity over the panel: no alpha accumulation on mesh seams.
static u32 button_ink(u32 color, bool enabled)
{
	if (enabled) return color;
	return C2D_Color32(6+((int)(color&255)-6)*0.4f,
		9+((int)((color>>8)&255)-9)*0.4f, 24+((int)((color>>16)&255)-24)*0.4f, 255);
}

static void touch_mark(int id, float x, float y, float pixels, float spacing)
{
	if (!touch_map_enabled(id)) return;
	u32 color = button_ink(rgb(0xc8cff0), touch_map_enabled(id));
	if (!ui_input_symbol(id, x, y, pixels*1.6f, color))
		ui_button_text(x, y, pixels, spacing, color, id);
}

static bool touch_button_lit(int i)
{
	int id = input_map_get(INPUT_PHYSICAL_N+i);
	return touch_map_enabled(id) && (touch_hit == i ||
		(id == INPUT_FF_TOGGLE && input_fast_forward()));
}

static u32 gradient(float t)
{
	if (t < 0) t = 0;
	if (t > 1) t = 1;
	return C2D_Color32(40-16*t, 58-24*t, 130-46*t, 255);
}

static void edge(float x, float y, float x2, float y2, float width, u32 color)
{
	C2D_DrawLine(x, y, color, x2, y2, color, width, 0.56f);
}

static void touch_panel_draw(tbutton_t b, bool enabled, bool hit)
{
	C2D_DrawRectSolid(b.x, b.y, 0.51f, b.w, b.h, button_ink(rgb(0x1f2a5a), enabled));
	if (hit)
		C2D_DrawRectangle(b.x+1, b.y+1, 0.52f, b.w-2, b.h-2,
			gradient(0), gradient(0), gradient(1), gradient(1));
	else C2D_DrawRectSolid(b.x+1, b.y+1, 0.52f, b.w-2, b.h-2, button_ink(rgb(0x0c1230), enabled));
}

static void quadrant_draw(int i)
{
	int id = input_map_get(INPUT_PHYSICAL_N+i);
	bool enabled = touch_map_enabled(id), hit = touch_button_lit(i);
	bool curved = touch_stick_active();
	tbutton_t b = touch_button(i);
	float sx = i & 1 ? 1 : -1, sy = i & 2 ? 1 : -1;
	// Draw merged faces first; one shared cut-out is painted after all faces.
	// This keeps a full-ring button seamless without a per-frame tessellation.
	touch_panel_draw(b, enabled, hit);
	u32 accent = button_ink(ui_input_color(id), enabled);
	if (hit) accent = C2D_Color32((accent&255)*0.65f+89, ((accent>>8)&255)*0.65f+89, ((accent>>16)&255)*0.65f+89, 255);
	// Accent only the outer cluster edges touched by this face.
	int cluster_x = 92;
	if (b.y == 12)
		C2D_DrawRectSolid(b.x, b.y, 0.58f, b.w, 3, accent);
	if (b.y+b.h == 228)
		C2D_DrawRectSolid(b.x, b.y+b.h-3, 0.58f, b.w, 3, accent);
	if (b.x == cluster_x)
		C2D_DrawRectSolid(b.x, b.y, 0.58f, 3, b.h, accent);
	if (b.x+b.w == cluster_x+216)
		C2D_DrawRectSolid(b.x+b.w-3, b.y, 0.58f, 3, b.h, accent);
	// Quarter centroids stay fixed. Merged halves use the middle of their
	// visible strip, keeping even the largest symbol clear of the stick ring.
	float lx = b.x+b.w/2.0f, ly = b.y+b.h/2.0f;
	if (curved) {
		if (b.w == 216 && b.h == 216) {
			lx = STICK_X + sx*63.8176f; ly = STICK_Y + sy*63.8176f;
		} else if (b.w == 216) ly = STICK_Y + sy*87;
		else if (b.h == 216) lx = STICK_X + sx*87;
		else { lx = STICK_X + sx*63.8176f; ly = STICK_Y + sy*63.8176f; }
	}
	touch_mark(id, lx, ly, g_new3ds ? 16 : 13, g_new3ds ? 3 : 2);
}

// Clip circle-border segments to bound faces. The rectangle/circle subtraction
// is shared with hit testing, including merged rows, columns and full rings.
static bool clip_border(float *x, float *y, float *x2, float *y2, tbutton_t b)
{
	float dx = *x2-*x, dy = *y2-*y, lo = 0, hi = 1;
	float p[] = { -dx, dx, -dy, dy };
	float q[] = { *x-b.x, b.x+b.w-*x, *y-b.y, b.y+b.h-*y };
	for (int j = 0; j < 4; j++) {
		if (fabsf(p[j]) < 0.0001f) { if (q[j] < 0) return false; }
		else if (p[j] < 0) lo = fmaxf(lo, q[j]/p[j]);
		else hi = fminf(hi, q[j]/p[j]);
	}
	if (lo > hi) return false;
	*x2 = *x+hi*dx; *y2 = *y+hi*dy;
	*x += lo*dx; *y += lo*dy;
	return true;
}
static void stick_cutout_draw(void)
{
	static float px[97], py[97];
	static float segments[112][4]; // at most 96 arc edges + 8 tile splits
	static unsigned previous_parts = ~0u;
	static int nsegments;
	if (px[0] == 0) {
		for (int j = 0; j <= 96; j++) {
			float a = j*(2*(float)M_PI/96);
			px[j] = STICK_X + 60*cosf(a); py[j] = STICK_Y + 60*sinf(a);
		}
	}
	unsigned parts = touch_parts();
	if (parts != previous_parts) {
		previous_parts = parts;
		nsegments = 0;
		for (int i = 0; i < 4; i++) if (touch_button_visible(i)) {
			tbutton_t b = touch_button(i);
			for (int j = 0; j < 96; j++) {
				float x = px[j], y = py[j], x2 = px[j+1], y2 = py[j+1];
				if (clip_border(&x, &y, &x2, &y2, b)) {
					float *s = segments[nsegments++];
					s[0] = x; s[1] = y; s[2] = x2; s[3] = y2;
				}
			}
		}
	}
	C2D_DrawCircleSolid(STICK_X, STICK_Y, 0.53f, 60, rgb(0x060918));
	for (int j = 0; j < nsegments; j++)
		edge(segments[j][0], segments[j][1], segments[j][2], segments[j][3], 1, rgb(0x1f2a5a));
}

static void stick_draw(void)
{
	C2D_DrawCircleSolid(STICK_X, STICK_Y, 0.60f, 50.75f, rgb(0x2b3a8c));
	C2D_DrawCircleSolid(STICK_X, STICK_Y, 0.61f, 49.25f, rgb(0x080c22));
	for (int d = -36; d <= 36; d += 18) {
		float extent = sqrtf(48.75f*48.75f-d*d);
		u32 c = C2D_Color32(90, 63, 184, 179);
		C2D_DrawLine(STICK_X+d, STICK_Y-extent, c, STICK_X+d, STICK_Y+extent, c, 1, 0.62f);
		C2D_DrawLine(STICK_X-extent, STICK_Y+d, c, STICK_X+extent, STICK_Y+d, c, 1, 0.62f);
	}
	float x = STICK_X+stick_x, y = STICK_Y+stick_y;
	C2D_DrawCircleSolid(x, y, 0.64f, 19.75f, rgb(0x6f86e8));
	C2D_DrawEllipse(x-18.25f, y-18.25f, 0.65f, 36.5f, 36.5f,
		gradient(0), gradient(0), gradient(1), gradient(1));
}

static void touch_rect_draw(int i, bool left)
{
	tbutton_t b = touch_button(i);
	int id = input_map_get(INPUT_PHYSICAL_N+i);
	bool enabled = touch_map_enabled(id), hit = touch_button_lit(i);
	touch_panel_draw(b, enabled, hit);
	u32 c = button_ink(ui_input_color(id), enabled);
	if (hit) c = C2D_Color32((c&255)*0.65f+89, ((c>>8)&255)*0.65f+89, ((c>>16)&255)*0.65f+89, 255);
	C2D_DrawRectSolid(left ? b.x : b.x+b.w-3, b.y, 0.58f, 3, b.h, c);
	touch_mark(id, b.x+b.w/2.0f, b.y+b.h/2.0f, g_new3ds ? 16 : 13, g_new3ds ? 3 : 2);
}

void touch_buttons_draw(void)
{
	if (!touch_buttons_active() && g_cfg.show_fps != 1) return;
	if (touch_buttons_active())
		C2D_DrawRectSolid(0, 0, 0.1f, 320, 240, rgb(0x060918));
	bool left = g_new3ds && g_cfg.touch_click_left;
	ui_text_pixels(NULL, left ? 308 : 12, 22, 11, 1.5f, 0,
		rgb(0xebeeff), left ? C2D_AlignRight : 0, "MENU");
	const unsigned header[] = { 0xd0313b, 0xe8b21c, 0x2fa36b, 0x2f6fc4 };
	for (int j = 0; j < 4; j++) {
		C2D_DrawRectSolid((left ? 266 : 12)+11*j, 27, 0.5f, 9, 2, rgb(header[j]));
		if (g_cfg.show_fps != 1)
			C2D_DrawRectSolid((left ? 246 : 12)+16*j, 220, 0.5f, 14, 2, rgb(header[j]));
	}
	if (g_cfg.show_fps != 1) ui_text_pixels(libui_font(), left ? 246 : 12, 214, 16, 0, 62, rgb(0x2b3360), 0, "PSX3D");
	if (!g_new3ds) {
		for (int i = 0; i < 4; i++) if(touch_button_visible(i)) quadrant_draw(i);
		if (touch_stick_active()) { stick_cutout_draw(); stick_draw(); }
	} else {
		for (int i = 2; i < 4; i++) if(touch_button_visible(i)) touch_rect_draw(i, left);
	}
	if (touch_button_visible(4)) touch_rect_draw(4, !left);
	if (touch_button_visible(5)) touch_rect_draw(5, !left);
}

// Menu-only lookup: use the existing persistent names for individual resets.
const char *input_control_key(int row)
{
	static char key[32];
	int i = control_item(row);
	if (i < 0)
		return NULL;
	if (i == INPUT_REMAP_N)
		return "touch_stick";
	if (i > INPUT_REMAP_N)
		return input_hotkey_key_name(i - INPUT_REMAP_N - 1);
	snprintf(key, sizeof(key), "map_%s", input_map_key_name(i));
	return key;
}
bool config_control_overridden(int row)
{
	const char *key = input_control_key(row);
	return config_in_game() && key && gamecfg_get(key);
}
