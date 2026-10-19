# Settings

Press **X** in the library for global settings, or choose **Settings** in the
pause menu for the current game. The top level groups Display, Performance,
Sound, Controls, Saves & general, Advanced and Reset to defaults.
The paused game stays visible on the top screen. Cheats are per game and open
from the pause menu; **B** there returns to the pause menu.

**A** opens a category or changes a row; **B** returns one level. **L/R** cycles
categories. Touch a row to select it and again to activate it. Each page remembers
its selection. Advanced groups CPU & compatibility, Speed hacks and Picture
accuracy. Debug tools appear only when explicitly enabled for development.

**Show FPS → FPS** reports new game pictures shown per second and game speed
relative to normal speed. A game that draws 30 pictures per second at full speed
reads **30 FPS** and **100% speed**. Automatic frameskip can keep game speed high
while showing fewer pictures. **Detailed** also shows emulated vblanks and host
loop iterations per second, labelled separately from shown pictures.

Each row states whether it affects **this game** or **all games**. Core options,
audio, smoothing, FPS and menu preferences are global even when opened during a
game. Screen mode, rewind and control mappings can have per-game overrides.
An asterisk marks an override. **X** clears the selected override and restores
the inherited preset/global value. **Y** resets the current game's controls
with confirmation.

**Reset this game** drops that game's overrides and restores its preset plus
global settings. **Reset all games** restores console defaults, including core
options and mappings, while retaining the library location and other games'
overrides. Neither action deletes saves or cheats. Confirm with a fresh **A**
press; **B** cancels.

Both consoles use GPU rendering. Rewind requires New 3DS; stereoscopic 3D is
temporarily unavailable in v0.4.0.
Uncurated games show every option supported by the console; presets can hide
features unsuitable for a particular game. Speed hacks can affect game timing
or graphics, so use the defaults unless testing a specific compatibility issue.

See [bottom-screen controls](../psx3d/docs/bottom-screen/README.md) for touch-slot
bindings and preset inheritance, and [remote testing](dev-control.md) for
capturing and navigating the actual settings UI during development.
