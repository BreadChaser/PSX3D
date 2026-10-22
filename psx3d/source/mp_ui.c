// Play together and Share PSX3D screens (MULTIPLAYER-DESIGN.md in the UI mock).
#include "psx3d.h"

// Main menu > Play together: the hub (Host a game / Join a game).
void mp_hub_run(void)
{
}

// Main menu > Share PSX3D.
void share_run(void)
{
	share_about_run(false);
}

// After mp_hub_run: the game Host a game picked (stub until the hub lands).
bool mp_launch_request(char *path, size_t len) { (void)path; (void)len; return false; }
