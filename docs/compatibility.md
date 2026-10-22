# Compatibility

What we have tested so far, and on which console. Games not listed haven't been tried yet; tell us how yours runs.

- Speed runs: real Old and New 3DS, one saved scene per game.
- Start-up checks: a PC simulator, 63 discs, about 100 seconds each, built-in BIOS.
- All tested discs are US (NTSC-U).

## Speed on real 3DS consoles (v0.5.0)

The busiest scene we found for each game. More: [Performance](../README.md#performance).

| Game | Old 3DS | New 3DS |
|---|---|---|
| Symphony of the Night | Full speed | Full speed |
| Final Fantasy VII (battle) | Full speed (the game draws 15 pictures a second) | Full speed |
| Klonoa | About 88%; full speed with Auto frameskip | Full speed |
| Crash Bandicoot | About 88%; about 98% with Auto | Full speed |
| Crash Bandicoot 2 | About 76%; about 92% with Auto | About 99% (frameskip off and Auto) |
| Crash Team Racing | About 79%; about 90% with Auto | Full speed |
| Ridge Racer Type 4 | About 83%; about 90% with Auto | Full speed |
| Tekken 3 | About 67%; about 92% with Auto | Full speed |
| Silent Hill | About 73–74%; about 77–86% with Auto | Full speed |
| Gran Turismo 2 (Arcade) | About 54% | About 94% with frameskip off, 99–100% with Auto |
| Need for Speed: High Stakes | About 36% | About 72% with frameskip off, about 98% with Auto |
| Mega Man X6 | About 97% in the first stage; full speed with Auto | Not measured |
| Resident Evil 2 (movies) | About 57% | Not measured |
| Final Fantasy VII (movies) | About 55% | Not measured |
| Metal Gear Solid (opening) | Not measured | About 82% frameskip off, about 100% Auto |

- Auto frameskip keeps at least 15 new pictures a second where it can.
- "Full speed" means 98% or more.
- Silent Hill covers two scenes (alley and café). The Old range shows both.

## Player reports

- Mega Man X6 ran at about 15 FPS on Old 3DS in v0.4.1 (issue #3). A texture bug was fixed in v0.5.0.
- Worms World Party runs slowly on Old 3DS (issue #3); we are looking at it.
- Resident Evil 2 (Spain, PAL) showed "lines on screen" for one player (issue #3). We couldn't reproduce it with the US disc; a photo would help.
- Capcom vs. SNK: the flash during a super move is part of the game.

## Start-up checks (PC simulator, 63 discs)

Each disc was started from scratch for about 100 seconds of game time with the built-in BIOS. A real BIOS ran the same discs for comparison.
This checks that the game starts and gets to the scene below, not speed or sound.

- 61 of 63 reached the scene named below.
- 2 did not: **Gran Turismo** (hangs after its loading screen with the built-in BIOS) and **Mega Man Legends 2** (black after the intro).
- The built-in BIOS keeps memory-card saves intact. A bug that could damage saves with the built-in BIOS was fixed in v0.4.0.

| Game (US disc) | Reached |
|---|---|
| Alundra (Rev 1) | Ship gameplay. |
| Ape Escape | Menus. Shows a "digital controller" warning until the controller is set to Analog; analog play not checked. |
| Blood Omen - Legacy of Kain | Opening tavern gameplay. |
| Brave Fencer Musashi | Opening forest gameplay. |
| CTR - Crash Team Racing | Adventure hub; a new save was created and loaded again (card data unchanged). |
| Castlevania - Symphony of the Night | Richter prologue gameplay. |
| Chrono Cross (Disc 1) | Name-entry menu. |
| Crash Bandicoot | N. Sanity Beach gameplay. |
| Crash Bandicoot - Warped | Warp-room opening; later gameplay not checked. |
| Crash Bandicoot 2 - Cortex Strikes Back | Warp-room gameplay. |
| Diablo (En,Fr,De,Sv) | Tristram gameplay. |
| Digimon World | Player-name entry. |
| Dino Crisis (Rev 1) | Opening playable sequence. |
| Driver - You Are the Wheelman (Rev 1) | Garage driving test. |
| Driver 2 (Disc 1) (Rev 1) | Street gameplay; missions not checked. |
| Einhaender | First-stage gameplay. |
| Final Fantasy Anthology - Final Fantasy VI (Rev 1) | Opening story after New Game; gameplay not checked. |
| Final Fantasy Chronicles - Chrono Trigger (Rev 1) | Title/load menu. |
| Final Fantasy IX (Disc 1) (Rev 1) | Empty-card load menu. |
| Final Fantasy Origins (Rev 1) | Game-selection title and the first game's load menu. |
| Final Fantasy Tactics | Name-entry menu. |
| Final Fantasy VII (Disc 1) | Title/New Game/Continue menu. |
| Final Fantasy VIII (Disc 1) | Empty-card load menu. |
| Gran Turismo | **Hangs** after its loading screen (black picture). Works with a real BIOS. |
| Gran Turismo 2 (Arcade Mode) (Rev 1) | Arcade race gameplay. |
| Grandia (Disc 1) | Empty-card load menu. |
| Harvest Moon - Back to Nature | Load-diary menu; saving not checked. |
| Klonoa - Door to Phantomile | Name entry and controller-settings menu. |
| Legacy of Kain - Soul Reaver | Opening sequence; control not checked. |
| Legend of Dragoon, The (Disc 1) | Title and opening town/movie; gameplay not checked. |
| Legend of Legaia | Title and opening story; gameplay not reached. |
| Medal of Honor | Opening forest-mission gameplay. |
| MediEvil | Dan's Crypt gameplay/dialogue. |
| Mega Man Legends 2 | **Black screen** after the opening movie, with and without a real BIOS. |
| Mega Man X4 | Opening-stage X gameplay. |
| Metal Gear Solid (Disc 1) (Rev 1) | Title menu. |
| Need for Speed - High Stakes | Race gameplay. |
| Oddworld - Abe's Oddysee (Rev 2) | RuptureFarms gameplay. |
| Parasite Eve (Disc 1) | Empty-card slot menu. |
| R4 - Ridge Racer Type 4 | Team-selection/car-data menus. |
| Rayman | Gameplay; a save was created and loaded again. |
| Resident Evil | Mansion opening; later progress not checked. |
| Resident Evil 2 - Dual Shock Ver. (Disc 1) | Title/load menu. |
| Resident Evil 3 - Nemesis | Title/load menu. |
| Silent Hill | Opening fog-street gameplay. |
| Soul Blade (Rev 1) | Character-selection menu. |
| Spider-Man | Rooftop gameplay. |
| Spyro - Year of the Dragon (Rev 1) | Sunrise Spring gameplay. |
| Spyro 2 - Ripto's Rage! | Glimmer gameplay. |
| Spyro the Dragon | Gameplay on a fresh card; loading saved progress not checked. |
| Star Ocean - The Second Story (Disc 1) | Opening playable sequence; battles not checked. |
| Street Fighter Alpha 3 | Fight (may be the demo). |
| Suikoden II | Load menu. |
| Tekken 3 | Fight (may be the demo). |
| Tenchu - Stealth Assassins (Rev 1) | Opening mission gameplay. |
| Tomb Raider (Rev 6) | Caves gameplay. |
| Tomba! | Opening playable village/dialogue. |
| Tony Hawk's Pro Skater 2 | Hangar gameplay. |
| Twisted Metal 2 | Arena gameplay. |
| Vagrant Story | Opening playable area. |
| Wild Arms | Empty-card load menu. |
| Wipeout XL | Race gameplay. |
| Xenogears (Disc 1) | Empty-card load menu. |

## Tell us what you find

Open an issue with your console, game, region and `log.txt` (see [CONTRIBUTING](../CONTRIBUTING.md)).
