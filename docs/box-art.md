# Box art

PSX3D downloads a game's picture once from the free libretro-thumbnails set and keeps a
finished copy on the SD card (`sdmc:/3ds/PSX3D/covers/`).

## How a picture is found

The set names its pictures after redump names ("Crash Bandicoot (USA)"). Your files can be
called anything, so PSX3D tries, in this order:

1. The file's own name, when the set has exactly that picture.
2. The disc's serial (read from the disc image's `SYSTEM.CNF`, or found in the file name,
   e.g. `SCUS-94900.chd`). `.cue/.bin`, `.iso`, `.img` and `.mdf` are read; `.chd` and `.pbp`
   are matched by file name only.
3. The same title with region, revision, disc and other tags, punctuation, upper/lower case and
   "The" ignored. Your file's own tags pick the region and disc (USA first when it has none).

## Pictures the set does not have

PSX3D remembers them (`covers/state.txt`) and does not ask again for 14 days. Pressing **SELECT**
in the library (rescan) asks again at once. With no Wi-Fi it waits longer between tries
(15 s, doubling up to 10 minutes).

## Games without a picture

They show a black PlayStation disc with the game's title on it. The disc is a photo from
Wikimedia Commons with the PlayStation wordmark, downloaded once the first time the library
has nothing else to fetch (`covers/_disc.bin`, about 256 KB); it is not part of PSX3D. Until
then, or without Wi-Fi, a disc drawn by PSX3D stands in.

- Photo: [Playstation disc.jpg](https://commons.wikimedia.org/wiki/File:Playstation_disc.jpg)
  by Zyliskiai, [CC BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/), cut out of its
  white background and shrunk.
- Wordmark: [PlayStation wordmark (1994-2009).svg](https://commons.wikimedia.org/wiki/File:PlayStation_wordmark_(1994-2009).svg),
  public domain (simple text), a trademark of Sony Interactive Entertainment. PSX3D is not
  made or endorsed by Sony.

## Your own picture

Put a PNG named like the game's file, without the extension, in `sdmc:/3ds/PSX3D/covers/`:

```
sdmc:/roms/psx/My Game.cue   →   sdmc:/3ds/PSX3D/covers/My Game.png
```

It wins over any download and replaces a picture that was already downloaded: replace or edit
the PNG and the cover follows the next time the library opens (or after a rescan); remove it and the downloaded
picture comes back. Any size works (it is scaled to fit 256×256 and keeps its shape). For a game
with several discs, name it after disc 1's file.
