# Release checklist

Run on a New 3DS with the **installed CIA** (not the netloaded .3dsx) before
every release. Each line should end with the app or the 3DS behaving normally,
with no freeze and no exception screen.

## Leaving and closing

| Where | Do | Expect |
|---|---|---|
| Library, covers loading | HOME, wait 10s, resume | covers continue loading |
| Library, covers loading | HOME → Close | back to HOME Menu |
| Library | START | back to HOME Menu |
| Library, covers loading | close the lid, open it | still running, covers continue |
| In game | HOME, resume | game and sound continue |
| In game | HOME → Close | back to HOME Menu, memory card saved |
| In game | close the lid, open it | game paused while closed, continues |
| In game | menu → Exit to game list → START | back to HOME Menu |
| Pause menu open | HOME → Close | back to HOME Menu |

## Starting

| Do | Expect |
|---|---|
| First start with no config.ini | library or folder browser, no crash |
| Start a game from the library | game runs with sound |
| Start a multi-disc game, swap discs in the menu | next disc boots |
| Save state, load state | game continues from the save |

## Performance spot checks

Silent Hill, Tekken 3, Crash Team Racing: about 60 FPS in play (FPS counter on).
