# Dynamic bottom-screen slots and curated presets

Branch `t3/bottom-screen-main`, based on released software-renderer main
`ea8d8c9`. All changes are in `psx3d/`. No push or console access.
Initial dynamic-slot implementation: `ae3c292` (bindings, tiling, persistence
and host checks). The curved-fill/action-palette follow-up adds no config keys.

## Slots and layout

The bottom screen is made of independently bindable slots. Each is hidden or
bound to a PS1 button or allowed action. **Hide removes the face, label and hit
target completely**; it no longer leaves a dim blank panel. The Controls page
lists empty slots too, so users can add controls omitted by a preset.

- Old 3DS: four cluster slots (top left/right, bottom left/right), right-stick
  slot, and side slots where their features are permitted.
- New 3DS: two stacked click slots (top/bottom) and allowed side slots. The
  physical C-stick supplies right analog; there is no touch-stick slot.
- Slot identities keep the existing `map_TOUCH_L2/R2/L3/R3/X1/X2` config names
  for compatibility. They describe positions, not fixed PS1 actions.
- Both consoles retain MENU and the PSX3D brand when any useful slot is bound.
  If none is bound, the screen is black, the backlight turns off after the
  existing 120-frame idle threshold, and any touch opens MENU. Show FPS
  intentionally keeps the backlight on.

With the Old right stick bound, bound faces fill the available cluster around
an r60 circular cut-out and the r50 grid pad at (200,120). Four buttons keep the
approved quarters. A lone button in a row spans that entire row as a half-ring;
two buttons in different rows fill the top/bottom halves. Two buttons sharing a
row instead occupy full left/right halves. One bound button fills the whole
square around the circular opening, with its accent along all four **outer**
square edges; the inner circle stays subtly bordered. Touch targets use exactly
those faces minus the r60 circle, with six-pixel gaps between separate faces.
The knob has r19, 31px maximum travel and a four-pixel dead zone. It feeds the full
signed `RETRO_DEVICE_INDEX_ANALOG_RIGHT` range and centers on release.

Without a stick, Old 3DS bound cluster buttons **tile the entire 216x216 area**, with
six-pixel gaps and no curved cut-outs. The full-screen panel is x0..320/y0..240. The centered cluster footprint is
x92..308/y12..228, leaving 12px at the top, right and bottom:

| Bound buttons | Layout |
|---|---|
| 0 | No cluster artwork or targets |
| 1 | One 216x216 square |
| 2 | Two 105x216 vertical halves; order follows left/right slot position, then row |
| 3 | Complete row keeps two 105x105 squares; third spans the other row at 216x105 |
| 4 | Four 105x105 squares in their original quarter positions |

Marks are centered in each actual face. Action-coloured accents hug
each face's outer cluster edges; internal gap edges retain the subtle border. Accents
follow the bound action, rather than the slot (palette below). With a stick,
quarter marks use the expanded faces’ visible-area centroids at offsets ±63.8176
from (200,120). Merged half marks sit 87px from the stick center on that half's
axis, inside the visible strip; the sole full-ring mark stays in its slot's
original quarter so it cannot cover the stick. The former cut-out area is
touchable only when it is filled by a rectangular face. The gaps never accept input.

The side footprint is x12..68/y64..176, with MENU above and the brand below:

| Bound side slots | Layout |
|---|---|
| 0 | Empty column |
| 1 | One 56x112 tall face, using that slot's binding and accent |
| 2 | Two 56x48 stacked faces, y64..112 and y128..176 |

Side slot 1 defaults to FF toggle, slot 2 rewind hold (both soft lavender), and each
remains remappable. Slot availability follows the game's permissions and engine
limits. Old 3DS cannot run rewind, so its second side slot is unavailable.
New 3DS exposes both when permitted, allowing both side-tiling variants.

New 3DS click buttons use the approved **edge column**, separate from the Old
3DS cluster. With both slots bound, top is x208..308/y12..114 (100x102), bottom
is x208..308/y126..228 (100x102). With exactly one bound, either slot fills
x208..308/y12..228 (100x216). None means no click-column faces or hit targets.
Marks stay centered and a 3px action-coloured accent runs only along the
screen-edge side (x305..308 in Right placement). The default L3/R3 accents
are cyan. Controls names are positional **Touch top / Touch bottom**, while
`map_TOUCH_L3/R3` keys and preset slot identities stay compatible.

Global **Stick-click buttons: Right / Left** controls New layout placement.
Left moves the click column to x12..112 (accent x12..15) and the side column
to x252..308, with MENU and brand on the right. MENU target x0..82/y0..40,
mirrored to x238..320. The brand remains 62px wide and its four-color bar ends
at the D. The Old 3DS cluster tiling never applies to this New 3DS column.

The action palette is shared by touch accents, symbols and the Controls page's
selected-row accent/icons. Shoulder labels keep their pale text for legibility;
list values keep the menu's normal text styling. The four PlayStation face
colours belong **only** to face-button bindings. Other actions use separate
library-inspired synthwave hues, regardless of the slot they occupy. The
four-colour MENU/brand bars remain unchanged.

| Binding | Accent / symbol colour |
|---|---|
| Triangle | Green `#2fa36b` |
| Circle | Red `#d0313b` |
| Cross | Blue `#2f6fc4` |
| Square | Pink `#d77fb5` |
| L1/R1/L2/R2 | Purple-magenta `#b05ade` |
| L3/R3 | Cyan `#45bfd0` |
| Directions, START/SELECT, FF toggle/hold, rewind hold, stick/hotkey rows | Soft lavender / grey-blue `#9aa5c8` |
| Hide / unmapped list rows | Muted `#2b3360`; no touch artwork |

Dark panel `#060918`, faces `#0c1230`, borders `#1f2a5a`, pressed blue gradient
and the approved color accents remain. Outline PlayStation face symbols and
arrows/START/SELECT/FF/rewind use vector shapes. Shoulder/stick names use
centered 13px text on Old /16px on New. FF hold/toggle share the same icon;
toggle slots stay lit for the effective FF state after release. Blank hidden
slots never draw. The resistive screen accepts one contact. A captured stick
drag never becomes a neighboring button press.

The old per-game **Touch controls Auto/On/Off row was removed**: independent
bindings make it redundant and avoid a hidden panel mask defeating a slot edit.
To hide everything, choose Hide for each slot and the stick. To restore the
profile, reset Controls. Existing On/Off INI settings migrate to independent
bindings when the game loads; the obsolete key is removed on the next controls
save/reset. On preserves custom mappings while binding otherwise inherited
slots; Off hides all touch slots and the stick. Auto requires no migration.

## Release v0.3.0 integration

Software is the factory renderer on both consoles. The per-console renderer
field now participates in startup selection; DEFAULT inherits global and no
seeded entry enables GPU. Explicit game choices and testing switches take
precedence. See [release validation](../../../docs/release-v0.3.0.md).

MENU, side slots and brand/FPS start 12px from the left edge; New Left
placement mirrors them to the right edge. Side slots remain vertically centered.
The in-game panel fills all 320x240 pixels; menu panels are unchanged.

FPS replaces the brand in its 62px corner: a 16px title-font rate (for example
`40.8 FPS`), a smaller game-speed percentage below it, and the same four-colour
bar ending at the text edge. New 3DS Left placement right-aligns both labels at
x308. Off restores the original brand. With no touch slots, FPS wakes/keeps the
bottom backlight on and draws only MENU and the readout on black. Tapping the
readout cycles to Detailed; its tap target no longer steals the bottom of the
New 3DS click buttons. Detailed retains its stats, MENU, and full-width footer.
Its hidden controls cannot receive touches. All drawing callbacks use
video_present and the PICA worker owner when that renderer is active.

FPS samples use the existing one-second interval and invalidate the retained
bottom target only when the displayed tenth of an FPS or whole speed percentage
changes. Hidden diagnostics do not trigger FPS redraws; interaction, OSD and
menu transitions can still repaint as needed. The FPS path skips detailed
formatting, option lookups and text-buffer reparsing during sample updates.

Mockups regenerated from production drawing calls (SVG and PNG; desktop fonts,
not console captures):
`fps-old-full`, `fps-new-stacked`, `fps-new-left`, and `fps-blank`.
Build checks: 3DSX and CIA. Host checks: `tests/statsview/check.c` covers visible
value invalidation, rounded unchanged values, speed changes, mirrored label/bar
alignment, and the existing Detailed formatter/cycling. The old one-line `fps/target fps % ms` draw has been removed; FPS draws only
the two corner labels and their bar. No console access.

## Presets, inheritance and permissions

Uncurated games expose every available setting, hack and touch control.
Old: full PS1 cluster/stick and FF side slot, FF targets/hotkey, no rewind.
New: two click slots, both speed side slots, FF/rewind targets/hotkeys; rewind
still needs per-game enablement in Settings. No other core-option trimming
was added. Curated entries can choose what each console needs independently.

The existing 32 serial entries preserve the six title groups and confidence
comments below. New needs L3/R3 and inherits speed features; Old keeps the full
safe PS1 layout, with FF disabled until tested. **No enabled curated Old FF
entry is seeded**, although unknown games expose FF as requested. No new
retail touch-requirement claims are invented for the mockups.

Binding precedence is per-game explicit override, global explicit override,
then per-console preset, then uncurated defaults. A hidden preset slot can be
bound by the user; a preset binding can be hidden. Changing the disc updates
only inherited slots. Unsupported speed actions never run; their stored
assignments survive for portability but are effectively hidden. Disallowed
hotkey combos consume nothing, so SELECT+L/R reach the game through normal
physical mappings. Permissions filter action targets, individual hotkey rows,
side-slot availability and the Rewind Settings row. Technical Old rewind limits
are enforced after preset resolution.

Global Controls reset clears explicit global bindings to factory/preset
inheritance. Per-game Controls reset drops that game's overrides, returning to
its profile plus global custom bindings. Per-game Settings reset also drops
screen/rewind and migrated legacy panel overrides, preserves the current disc
profile, and stops rewind. Global Settings reset restores console defaults
including core options and all controls, retaining paths/game files/saves/cheats.
All resets ask for a fresh A; B cancels.

## Preset structure and renderer extension

`source/presets.c` resolves `source/game_presets.h`. Every record contains
separate `old3ds` and `new3ds` option records, including renderer:

```c
typedef struct {
    int fast_forward, rewind, touch_controls;
    int needs_l2r2, needs_l3r3, needs_right_stick;
    int renderer;
    int touch_bindings[INPUT_TOUCH_N];
} game_preset_options_t;
typedef struct {
    const char *serial;
    game_preset_options_t old3ds, new3ds;
} game_preset_t;
```

Boolean fields use -1 inherit /0 false /1 true. The retained **built-in**
`touch_controls` policy uses -1 inherit or Auto0/On1/Off2 as an initializer
shorthand. It is not a per-game visibility mask. Auto uses needs fields and
explicit preset slot bindings; On initializes the full console layout; Off
initializes it empty. User slot edits take precedence.

`touch_bindings` order is TL, TR, BL, BR, side1, side2. Values are -2 to inherit
the needs/policy shorthand, -1 Hide, or a PS1/action ID. Initialize **every slot**;
omitted C array members are zero (Cross), not inherited. New uses BL/BR indices
for its top/bottom click slots, retaining the old map keys. `needs_right_stick` supplies
the stick's preset binding. An explicit Old user stick value overrides it.

Renderer values: `PRESET_RENDERER_INHERIT` (-1), `DEFAULT` (0), `SOFTWARE` (1),
`GPU` (2), all with the `PRESET_RENDERER_` prefix. This main build resolves the
field as metadata only; renderer selection is for the later integration.
Software remains a permanent supported renderer.

`GAME_PRESET_SAME` duplicates a complete initializer for both console records:

```c
// Illustrative, not a shipped or verified game ID:
GAME_PRESET_SAME("ABCD12345", {
    .fast_forward = 0, .rewind = 0,
    .touch_controls = TOUCH_CONTROLS_AUTO,
    .needs_l2r2 = 0, .needs_l3r3 = 0, .needs_right_stick = 0,
    .renderer = PRESET_RENDERER_DEFAULT,
    .touch_bindings = { 12, 13, -1, -1, -1, -1 },
}),
```

For different consoles, use `{ .serial="...", .old3ds={...}, .new3ds={...} }`.
`INHERIT_SLOTS` and `CLICKS` in the table keep entries short. A commented-only
Old FF example remains; do not enable it without measurement. For future
frameskip/clock/renderer parameters add explicit inheritance, uncurated defaults,
resolver assignments, initializers/shorthands and validation. Keep source and
confidence beside each serial. No per-frame serial lookup: the existing core
`CD-ROM ID: <serial>` log is normalized/validated once and caches the record.
Resolved capabilities, the visible-slot mask and clipped circle-border geometry are cached; drawing only runs
on changes to interaction, bindings, profile, placement, FF state or overlays.
Unknown/invalid IDs clear the prior profile; unrelated log lines do not.

## Configuration and migration

Global `config.ini` now writes `touch_slots=1` as a migration marker and stores
**raw inherited/explicit bindings**, never resolved game bindings:

```ini
touch_slots=1
map_TOUCH_L2=-2
map_TOUCH_R2=-2
map_TOUCH_L3=-2
map_TOUCH_R3=-2
map_TOUCH_X1=-2
map_TOUCH_X2=-2
touch_stick=-1
touch_click_left=0
hotkey_ff=SELECT+R
hotkey_rewind=SELECT+L
```

For touch map keys: -2 means inherit preset/default, -1 means Hide; 0..15 are
PS1 buttons, 16 FF toggle, 17 FF hold, 18 rewind hold. Physical mappings retain
-1=nothing and normal button/action IDs; physical -2 is invalid. Existing
`map_A/B/X/Y/L/R/ZL/ZR/START/SELECT` names remain. No action enters the PS1 bitmask.
`touch_stick`: -1 inherit, 0 Hide, 1 right stick. `touch_click_left` remains
global only (Right0/Left1). All bindings/stick/hotkeys support per-game overrides.

Legacy global files without `touch_slots=1` saved all default touch maps even
when untouched. Those factory-valued maps and `touch_stick=1` now migrate to
inheritance, so presets reach old installs. Other explicit legacy remaps/hides
remain. Per-game maps were saved only when different and retain their intent.
Legacy per-game `touch_controls=1/2` migrates as described above; new saves no
longer write that key. Obsolete `map_TOUCH_SELECT` is ignored/cleaned on save.
Per-game saves store only differences from globals. Saving globals while a game
runs writes the global raw bindings, never that game's effective profile.

## Seed table and evidence

Confidence below concerns **L3/R3 use**, separately from verified disc serial
identity. The user requested all six titles, including the five lower
confidence candidates. Every regional ID listed here was checked against
[PlayStation DataCenter US](https://psxdatacenter.com/ulist.html),
[PAL](https://psxdatacenter.com/plist.html),
[Japan/Asia](https://psxdatacenter.com/jlist.html), and/or the linked title page.
IDs are stored without punctuation in `psx3d/source/game_presets.h`; each title
has an explicit confidence comment. No unverified demo IDs are included.

| Title | L3/R3 confidence | Verified serials included |
|---|---|---|
| [Ape Escape / Saru! Get You!](https://psxdatacenter.com/games/U/A/SCUS-94423.html) | **High:** user-confirmed crawl, dive, underwater net and hide; manual confirms stick-click crouch/crawl, DataCenter lists L3 swim down and R3 throw net | US `SCUS-94423`; PAL `SCES-01564`, `SCES-02028`, `SCES-02029`, `SCES-02030`, `SCES-02031`; JP/Asia/reissues `SCPS-10091`, `SCPS-45411`, `SCPS-91196`, `SCPS-91331` |
| [Omega Boost](https://psxdatacenter.com/games/U/O/SCUS-94449.html) | **Low:** user-supplied account of one forum post testing a demo disc; retail use not independently confirmed | US `SCUS-94449`; PAL `SCES-02120`; JP `SCPS-10020`; Asia `SCPS-45399` |
| [Twisted Metal 4](https://psxdatacenter.com/games/U/T/SCUS-94560.html) | **Low:** same demo-disc report | US `SCUS-94560` |
| [Tomb Raider: The Last Revelation](https://psxdatacenter.com/games/U/T/SLUS-00885.html) | **Low:** same demo-disc report | US `SLUS-00885`; PAL `SLES-02238`, `SLES-02239`, `SLES-02240`, `SLES-02241`, `SLES-02242`; JP/reissue `SLPS-02803`, `SLPM-86896` |
| [Ace Combat 3: Electrosphere](https://psxdatacenter.com/games/U/A/SLUS-00972.html) | **Low:** same report; Japanese two-disc edition differs substantially and needs separate retail control verification | US `SLUS-00972`; PAL `SCES-02066`; JP/Asia two-disc editions `SCPS-45397`, `SCPS-45398`, `SLPS-02020`, `SLPS-02021`, `SLPS-91214`, `SLPS-91215` |
| [Supercross Circuit](https://psxdatacenter.com/games/U/S/SCUS-94453.html) | **Low:** same demo-disc report | US `SCUS-94453` |

Additional primary evidence: [Ape Escape US instruction manual scan](https://www.gamesdatabase.org/Media/SYSTEM/Sony_Playstation/Manual/formated/Ape_Escape_-_1999_-_Sony_Computer_Entertainment.pdf),
PDF pages 4 and 6: "press and hold [the left stick] down (like a button)" to
crouch/crawl. Its scanned pages required OCR/visual inspection.
The lower-confidence forum post's URL was not supplied; those five entries
are included on the user's explicit instruction, not presented as independently
confirmed controls. DataCenter's abbreviated control lists do not establish
L3/R3 use for them. Regional releases inherit the same confidence; serial
verification does not prove identical controls across regions. Only regions
and reissues located in the cited catalog are included.

## Validation and mockups

Passed: production host checks under ASan/UBSan, `make -j16` (3DSX), and CIA
packaging with the local bannertool/makerom. Existing unrelated compiler/linker
warnings remain. Artifacts: `psx3d/PSX3D.3dsx` and `psx3d/PSX3D.cia` (ignored).

```sh
psx3d/tests/host/run.sh
source /home/tony/Development/PS1_3ds/devtools/env.sh
make -j16
make cia \
 BANNERTOOL=/home/tony/Development/PS1_3ds/devtools/tools/bannertool/output/linux-x86_64/bannertool \
 MAKEROM=/home/tony/Development/PS1_3ds/devtools/tools/Project_CTR/makerom/bin/makerom
```

Host checks exercise all 15 nonempty cluster masks with and without the stick, every pixel of
each target and non-overlap, curved merges and full-ring ownership, the action palette, all tiling counts/areas, empty-slot edits through
the Controls callback, stick bind/hide/range/centering, zero/one/two side slots,
New Right/Left and all zero/one/two click masks (every column pixel and gap), all allowed actions on every slot,
hotkeys/toggle latch/cache/menu blocking, disallowed-combo passthrough,
capability revocation, per-console preset/renderer/serial resolution,
global/per-game persistence and every reset, raw inheritance and legacy
migration, symbol/text ink centers and fresh-A/cancel confirmation.

Mockups use real production drawing calls at 2x native 320x240. Michroma is the
bundled title font; fontconfig requests DejaVu Sans for the console system
font substitute (Liberation Sans fallback in the latest New 3DS mockups).
Preset scenarios are **test fixtures**, not unverified retail entries.

| Mockup | Scenario |
|---|---|
| [L2/R2 preset](bottom-l2r2-only.png) | Two bound slots, tall halves |
| [Bind bottom-left to triangle](bottom-l2r2-triangle.png) | Two top squares + one wide lower face |
| [Also bind the stick](bottom-l2r2-triangle-stick.png) | Two top quarters, full green lower half-ring and stick |
| [Stick + one](stick-one-button.png) | Full ring, label clear of stick, outer perimeter accent |
| [Stick + two rows](stick-two-rows.png) / [diagonal](stick-two-diagonal.png) | Full top/bottom half-rings |
| [Stick + two columns](stick-two-columns.png) | Full left/right half-rings |
| [One](bottom-one-button.png) / [two](bottom-two-buttons.png) / [three](bottom-three-buttons.png) / [four](bottom-four-buttons.png) | Cluster tiling counts |
| [One side](side-one-button.png) / [two sides](side-two-buttons.png) | Side tiling on New with other slots hidden |
| [Side toggle lit](side-one-button-lit.png) | FF stays lit after release |
| [Old side](old-side-one-button.png) | Old FF side fills its area |
| [Empty slots in Controls](controls-empty-slots.png) / [bound](controls-bound-slot.png) | Hide versus triangle |
| [Uncurated Old](bottom-idle.png) / [curated Old](bottom-curated-full.png) | Full safe layouts with/without FF |
| [New two clicks](new3ds-l3r3.png) / [one click](new3ds-one-button.png) | Stacked edge column / lone slot fills column |
| [New lone R3](new3ds-one-r3.png) / [Left lone R3](new3ds-left-one-r3.png) | Either slot fills the same column |
| [New Left](new3ds-left.png) | Edge-column/side/menu/brand mirrored placement |
| [Blank](bottom-blank.png) | No bound slots; tap anywhere MENU |
| [Global reset](reset-global.png) / [game reset](reset-game.png) | Confirmation dialogs |

## Hardware checks remaining

No consoles were touched. The console owner should verify:

1. Old curated L2/R2 fixture: bind an empty lower slot to Triangle, verify the
   wide tile works, then bind right stick: Triangle spans the lower half-ring
   with a green accent. Check merged row/column/full-ring targets and axes.
   Hide slots and check all 1/2/3/4 tilings, gap behavior and touch feel.
2. Hide every slot: black bottom/backlight idle and tap-anywhere MENU. Rebind
   one slot, verify wake after the elapsed idle threshold, and check HOME/resume.
3. Old uncurated game: full PS1 layout + tall FF side, no rewind UI; curated
   game with FF disallowed: no side, no FF remap/hotkey, SELECT+R passes through.
4. New: one/two click slots, Right/Left with MENU/brand on the clear side,
   one/two side slots, FF latch, rewind hold with history enabled, physical
   C-stick/ZL/ZR/SELECT and remaps. Check the narrow side targets by finger.
5. Global versus per-game custom slots/stick/hotkeys, restarting, migration,
   Controls/game/global reset and cancel. Reset should restore preset plus
   explicit global bindings, with saves/cheats kept.
6. Validate low-confidence retail/region L3/R3 claims before upgrading them.
   Renderer selection and further performance preset fields remain for later.

[Rewind budget and CPU estimates](rewind-budget.md): Old workspace is 17 MiB
before history versus about 18.7 MiB total ordinary heap; even a 4 MiB history
would require about 21 MiB. Capture+scan traffic estimate is about 125 ms before
compression overhead. These are code/historical-budget estimates, not new
hardware measurements; the engine remains technically unavailable on Old.
