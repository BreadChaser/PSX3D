PSX3D-WS-1
# Game: Spyro the Dragon (USA)
# Serial: SCUS94228; boot executable FNV-1a: 05FE587F
# Source: libretro/libretro-database@12c21cf79f1e20eff6ac68b80875816c717d0a7a
# File: cht/Sony - PlayStation/Spyro the Dragon (USA).cht; cheat0
# Licence: CC-BY-SA-4.0; libretro contributors.
# Changes: complete entry; guarded originals from matching unpatched simulator RAM for safe state rollback.
# Culling: Includes projection and companion A7BB instruction; needs edge validation
# HUD/2D: No separate HUD fix
# Issues: upstream DuckStation patch warns "May Cause Crashes"; scene-limited testing cannot rule this out.
aspect=16:9
[complete bundle]
A701EDDE 0000241B
A701EDDC 00000C00
A7033C92 A7B1A7BB
