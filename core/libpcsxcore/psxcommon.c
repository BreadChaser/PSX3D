/***************************************************************************
 *   Copyright (C) 2007 Ryan Schultz, PCSX-df Team, PCSX team              *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   51 Franklin Street, Fifth Floor, Boston, MA 02111-1307 USA.           *
 ***************************************************************************/

#include "psxcommon.h"
#include "r3000a.h"
#include "psxbios.h"

#include "cheat.h"
#include "ppf.h"

PcsxConfig Config;
// Frontend-owned curated patches run at the same vblank boundary as cheats.
void (*psx3d_widescreen_hook)(void);
void (*psx3d_widescreen_state_hook)(void);
void psx3d_patch_clear(uint32_t address, unsigned bytes)
{
	psxCpu->Clear(0x80000000u | address, bytes / 4);
}

int EmuInit() {
	return psxInit();
}

void EmuReset() {
	FreeCheatSearchResults();
	FreeCheatSearchMem();

	psxReset();
}

void EmuShutdown() {
	ClearAllCheats();
	FreeCheatSearchResults();
	FreeCheatSearchMem();

	FreePPFCache();

	psxShutdown();
}

void EmuUpdate() {
	ApplyCheats();
	if (psx3d_widescreen_hook) psx3d_widescreen_hook();

	// reamed hack
	{
		extern void pl_frame_limit(void);
		pl_frame_limit();
	}
}
