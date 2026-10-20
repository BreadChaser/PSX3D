# PSX3D v0.4.1

## Highlights

- **Old 3DS: much faster in busy scenes.** The renderer does far less work per
  frame. In each game's heaviest tested scene with frameskip off: Crash
  Bandicoot 2 goes from 48% to 70% game speed (86% with Auto frameskip), Crash
  Team Racing from 59% to 75%, Tekken 3 from 53% to 64% (87% with Auto
  frameskip), Silent Hill from 60% to 67–68%, and Ridge Racer Type 4, Gran
  Turismo 2 and Need for Speed: High Stakes about 10%.
- **New 3DS: every tested heavy scene runs at full speed**, now including Need
  for Speed: High Stakes.
- **Fixed: HOME → Close froze on the "Closing software" screen** while the
  game's sound kept playing (GitHub issue #1). Closing now takes about a second
  and still saves your place for "Continue where I left off".
- **Fixed a New 3DS crash** in very busy start-up sequences: Crash Bandicoot 2's
  intro, and the openings of Metal Gear Solid, Grandia and Oddworld: Abe's
  Oddysee. Pictures that are too big for one go are now sent to the GPU in parts.
- **Texture handling no longer slows busy scenes down.** The GPU texture cache no longer thrashes:
  Ridge Racer Type 4 races on Old 3DS go from 33% to 78% game speed (87% with
  Auto frameskip). On New 3DS, Need for Speed: High Stakes goes from 48% to
  full speed, and Ridge Racer Type 4 and Tekken 3 now hold full speed.
- **Klonoa runs about 45% faster on Old 3DS**: 84% game speed in its busiest
  tested scene instead of 58%, and 98% with Auto frameskip. The game spends much of each
  frame asking the console whether the next frame has started; the emulator now
  skips those repeated questions without changing the game's timing.
- **Crash Bandicoot runs about 3% faster on Old 3DS** (96% with Auto
  frameskip) by skipping its wait loop the same way.
- **Faster PS1 emulation on Old 3DS** through quicker scratchpad and memory access.
- **Real widescreen** for 22 games with a tested widescreen fix (20 on Old 3DS), among them
  Ridge Racer Type 4, Gran Turismo 2, Crash Bandicoot 2 and 3, Spyro 2 and 3,
  Ape Escape, Metal Gear Solid, Medal of Honor and Vagrant Story; Crash Team
  Racing and Silent Hill on New 3DS. Games without a fix offer only 4:3 and
  Stretch, so nothing gets cut off at the edges. On Old 3DS, Gran Turismo 2
  offers Widescreen with a warning that it can lower the speed (about 2% in a
  busy race, 4–6% in a lighter scene).
- **Cleaner menus:** settings opened in a game belong to that game, help text on
  the top screen, a new folder browser, and Cheats only in the pause menu.
- **Dithering on by default**, like a real PlayStation: no more colour bands in
  Silent Hill's fog.
- Fixed the thin blue line at the top of Crash Bandicoot's picture.

PSX3D remains a work in progress. The GPU renderer still has drawing errors and
missing effects.

## Performance

Measured in each game's heaviest tested scene. Full chart and numbers:
[README](../README.md#performance), method: [performance.md](performance.md).

| Scene | Old 3DS v0.4.0 → v0.4.1 (frameskip off) | New 3DS v0.4.0 → v0.4.1 |
|---|---|---|
| Ridge Racer Type 4 race | 33% → 78% | 88% → 100% |
| Crash Bandicoot 2 | — → 70% | 100% |
| Crash Team Racing race | 60% → 75% | 100% |
| Klonoa | — → 84% | 100% |
| Tekken 3 | — → 64% | 96% → 100% |
| Need for Speed: High Stakes | — → 35% | 48% → 100% |
| Silent Hill café / garage | 60% → 68% / 71% → 77% | 100% |
| Crash Bandicoot | 83% → 85% | 100% |
| Symphony of the Night fight | 97% → 96% | 100% |

With Auto frameskip, Old 3DS Klonoa reaches 98%, Crash Bandicoot 96%, Tekken 3
87%, Ridge Racer 87%, Crash Bandicoot 2 and Crash Team Racing 85–86%, and
Symphony of the Night and Final Fantasy VII battles run at full speed.
(The Old 3DS v0.4.1 runs were made while it streamed its screen over Wi-Fi,
which costs it 1–3%.)
FPS in the chart now counts new pictures, like the in-game counter: Final
Fantasy VII battles draw 15 pictures a second, with or without frameskip.

## What changed

- Renderer: blended drawing waits so it can be grouped with later drawing;
  additive effects take one pass; shader settings are only sent when they
  change; textures are placed clear of rows the GPU draws into, so Crash Team
  Racing no longer waits for the GPU several times a frame.
- Dynarec: Tekken 3's polygon code now runs as compiled code instead of the
  slower interpreter (a register read in a branch's delay slot is compiled
  exactly).
- HOME → Close: the game's place is saved when the HOME Menu opens, and closing
  no longer waits for the GPU, which the HOME Menu has taken over.
- Very busy frames are handed to the GPU in parts instead of overflowing its
  command queue (the New 3DS crash).
- Texture atlas: textures take exactly the space they need, and a full atlas
  reuses the least recently used space of any size. Long sessions no longer slow
  down (Silent Hill's café), and busy 3D scenes stop re-decoding textures.
- Dynarec: the PS1 scratchpad is mapped at an address one instruction can
  reach; neighbouring loads and stores share one range check; scratchpad stores
  skip a needless code check; the ordering-table clear is unrolled. Also fixes a
  rare register mix-up for games that read addresses from the real BIOS.
  Waiting loops of the PsyQ library (`VSync` polling, `v_wait`) are skipped
  exactly, and generated code no longer carries branch tricks meant for
  other ARM chips.
- Widescreen comes from per-game, per-revision fixes (credits in CREDITS), applied
  and removed safely, kept across save states and disc changes. On Old 3DS,
  Crash Team Racing and Silent Hill keep Widescreen off to keep the speed up
  (Settings says so), and Gran Turismo 2 offers it with a speed warning.
- Settings: from the library they apply to all games; from a game, to that game
  (changed rows are marked with \*, X goes back to the all-games value). Top-screen
  help for every option. Plainer option names.
- "Continue where I left off" is always on (the setting is gone).
- Pause menu: Reset asks first; Exit stays visible with Swap disc.
- Library: SELECT rescans and says how many games it found; collections show
  their covers stacked; long names end in "...".
- Touch L3/R3 appear only while a game uses analog mode.
- Logs keep the last five sessions.
- Old 3DS: the core now starts at a fixed code offset, so menu changes can't
  slow the emulator through code layout.

## Known limits

- GPU rendering can show incorrect effects, flicker and other drawing errors.
- Stereoscopic 3D is still unavailable (the 3D slider shows a flat picture).
- Rewind requires New 3DS.
- Display centering and interlaced modes are not implemented.
- Heavy Wi-Fi traffic and slow cover downloads can cause audio clicks.

## Install

Scan this code in **FBI → Remote Install → Scan QR Code**, or download
[**PSX3D.cia**](https://github.com/BreadChaser/PSX3D/releases/latest/download/PSX3D.cia)
and install it with FBI. A 3DSX is available for Homebrew Launcher.

![FBI remote install QR code](https://raw.githubusercontent.com/BreadChaser/PSX3D/main/docs/qr-install.png)

Existing settings and saves are kept.

Credits and GPL-2.0 licensing: [README](../README.md#credits-and-licence) and [LICENSE](../LICENSE).
