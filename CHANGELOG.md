# Changelog

What changed in each release, newest first. One line per point.
Speed figures are for specific saved scenes; see [Performance](README.md#performance) and [compatibility](docs/compatibility.md).

## v0.5.1

- New: a main menu with Continue, Library, Share PSX3D and Settings.
- New: the library shows recently played games first; sort by name, recent or most played; mark favourites with Y; L/R jumps by letter.
- New: touch buttons at the bottom of every menu.
- New: save backups: the last ten automatic saves and dated memory card copies per game; Undo and Redo in the pause menu.
- New: sharper box art, found by disc serial for files with any name.
- New: PSX3D finds your games on first start; only PlayStation discs are listed.
- New: clear messages when a game can't start (missing file, wrong disc, not enough memory).
- New: European and Japanese discs of the tested games get their settings too.
- New: Real 3D gives 2D games and menus layered depth; logos, movies and stills keep screen depth.
- New: covers show at once while you scroll; games without box art show a disc with their title.
- Fixed: Real 3D cracks between polygons, blurry logos and jumping text.
- Fixed: Real 3D flicker and sliced logos in 2D games (Street Fighter Alpha, Castlevania's left edge); busy 2D fighters show screen depth.
- Faster: Old 3DS in busy scenes, about 4% on average: Crash Bandicoot 2 76% to 85% game speed, Silent Hill 77% to 87% (café, Auto), Gran Turismo 2 54% to 58%.
- Fixed: the picture sits where a TV shows it, including borders, 480i and PAL movies.
- Fixed: leftover pictures above and below Resident Evil 2 movies with frameskip on.
- Fixed: a possible New 3DS crash with Real 3D in busy game start-ups (Crash Bandicoot, Harvest Moon).
- Fixed: memory cards are checked before a backup is replaced; damaged cards ask what to do.
- Known: Play together comes in a later version.

## v0.5.0 (2026-10-21)

- New: Real 3D on New 3DS (experimental). Raise the 3D slider in a game.
- New: Controller setting (Digital or Analog).
- New: rewind is on by default on New 3DS.
- New: a faster PS1 processor for Crash Team Racing, Gran Turismo 2 (Arcade), Silent Hill and Need for Speed: High Stakes (US discs, New 3DS).
- New: the settings are grouped as Display, Controls, Saves, Advanced and Reset; the "4:3" screen choice is now called "Original".
- New: game settings update themselves, checked once a day while you're in the library.
- New: the library shows a notice when fast mode is off (PC emulator or no Luma3DS).
- Faster: Old 3DS with Auto frameskip, Silent Hill +3.5–6.3%, Gran Turismo 2 +3.6%, Ridge Racer Type 4 +1.9%, Crash Bandicoot 2 +1.4% (busiest scenes, against v0.4.1).
- Faster: Metal Gear Solid's opening on New 3DS (about 23% to 82% game speed with frameskip off, full speed with Auto).
- Faster: Resident Evil 2 movies on Old 3DS by about 15%; they still run at about 55–60%.
- Fixed: Mega Man X6 rebuilt its textures every frame (about 15 FPS on Old 3DS).
- Fixed: thin black lines at polygon edges (Silent Hill, Tekken 3, Castlevania).
- New: Continue also saves when the console sleeps and every few minutes.
- Fixed: memory cards and save states keep a backup and recover from a damaged file; two games with the same file name no longer share saves.
- Fixed: audio keeps its pitch when a game runs slow.
- Known: Need for Speed: High Stakes is about 4% slower on Old 3DS (the PS1 sound echo is always on now).
- Known: Metal Gear Solid's opening needs Auto frameskip for full speed on New 3DS.

## v0.4.1 (2026-10-20)

- Faster on Old 3DS in busy scenes: Crash Bandicoot 2 48% to 70%, Crash Team Racing 59% to 75%, Tekken 3 53% to 64%, Silent Hill 60% to 67–68%.
- Faster: Klonoa about 45% and Crash Bandicoot about 3% on Old 3DS, by skipping the games' wait loops.
- Faster: Ridge Racer Type 4 (Old 3DS 33% to 78%) and Need for Speed: High Stakes (New 3DS 48% to full speed) after a texture cache fix.
- New: real widescreen for 22 games with a tested fix (20 on Old 3DS).
- New: Gran Turismo 2 widescreen on Old 3DS, with a warning that it can lower the speed.
- New: dithering is on by default, like a real PlayStation.
- New: per-game settings, help text on the top screen and a new folder browser; Cheats only in the pause menu.
- New: "Continue where I left off" is always on; logs keep the last five sessions.
- Fixed: HOME → Close froze on "Closing software" (issue #1).
- Fixed: a New 3DS crash in very busy start-ups (Crash Bandicoot 2's intro, Metal Gear Solid, Grandia, Oddworld: Abe's Oddysee).
- Fixed: the thin blue line at the top of Crash Bandicoot's picture.
- Known: the GPU renderer still has drawing errors and missing effects.
- Known: no display centering or interlaced modes.
- Known: heavy Wi-Fi traffic and slow box art downloads can cause audio clicks.

## v0.4.0 (2026-10-19)

- Changed: GPU rendering is the only renderer on Old and New 3DS; the Software renderer is gone.
- New: smarter frameskip (Off or Auto, at least 15 pictures a second).
- New: a built-in replacement BIOS, so games start without a BIOS file.
- New: automatic box art on both console models, with lower memory use.
- New: the FPS display shows new pictures per second next to game speed.
- New: clearer settings with categories, per-game overrides and reset confirmations.
- Faster: Old 3DS Crash Team Racing +15%, Gran Turismo 2 +6%, Silent Hill +7% against v0.3.1.
- Fixed: memory-card saves could be damaged when a game created a multi-block file.
- Fixed: movie sync, border clearing, and a corrupted menu after leaving a game.
- Known: the GPU renderer still has drawing errors, flicker and a thin blue line at the top of Crash Bandicoot's picture.
- Known: no display centering or interlaced modes.

## v0.3.1 (2026-10-18)

- Faster: Old 3DS 6–13% against v0.3.0 with frameskip off.
- Faster: full game speed with Auto frameskip on Old 3DS in Crash Bandicoot, Ridge Racer Type 4 and Symphony of the Night.
- Fixed: save-state previews on the GPU renderer, and box art on Old 3DS.
- Known: the GPU renderer still has drawing errors, missing effects and flicker.
- Known: Crash Team Racing, Gran Turismo 2 and Silent Hill run below full speed on Old 3DS.

## v0.3.0 (2026-10-18)

- New: a GPU renderer for the 3DS (experimental), up to twice as fast as the software renderer on Old 3DS.
- New: per-game presets for Old and New 3DS.
- New: a bottom screen with only the touch controls a game needs, remappable per game.
- New: Show FPS, smooth audio when slow, reset to defaults, remappable fast forward and rewind.
- Known: the GPU renderer shows drawing errors, missing effects or flicker in some games.
- Known: no display centering or interlaced modes.

## v0.2.0 (2026-10-15)

- New: fast forward (SELECT + R) and rewind (SELECT + L, New 3DS).
- New: Continue where you left off, button remapping per game, cheats, and global settings from the library.
- New: help text for every option on the top screen.
- New: Old 3DS touch buttons for L2/R2 and faster Old 3DS defaults.
- Known: widescreen without a game-specific code leaves empty edges.
- Known: Crash Team Racing can get stuck on its attract-mode loading screen.
- Known: on Old 3DS the Continue screen shows a green box instead of the picture.

## v0.1.1 (2026-10-15)

- New: series grouping in the library.
- New: a Cheats page with about 2,000 cheat lists, and your own `.cht` files.
- New: widescreen codes for a few games.
- Fixed: the list scrolled back to the top when leaving a series.

## v0.1.0 (2026-10-15)

- First release: a PlayStation emulator for the New 3DS, built on PCSX-ReARMed.
- Game library with box art, save states and memory cards.
- Known: some games need a real BIOS to start.
