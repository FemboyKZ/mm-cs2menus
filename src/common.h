#ifndef _INCLUDE_MENU_COMMON_H_
#define _INCLUDE_MENU_COMMON_H_

#include "mmu/chat_colors.h"
#include "mmu/plugin_globals.h"

#include <ISmmPlugin.h>
#include <igameevents.h>
#include <iserver.h>
#include <sh_vector.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

// A player slot in the valid 0..MAXPLAYERS-1 range.
// The bound is exclusive: slots are 0 based, and MAXPLAYERS matches the engine's ABSOLUTE_PLAYER_LIMIT,
// so slot MAXPLAYERS has no bit in a CPlayerBitVec.
inline bool ValidSlot(int slot)
{
	return slot >= 0 && slot < MAXPLAYERS;
}

// Plugin-specific engine interfaces. Shared ones live in mmu/plugin_globals.h.
extern INetworkServerService *g_pNetworkServerService;

class INetworkMessages;
class IGameEventSystem;
extern INetworkMessages *g_pNetworkMessages;
extern IGameEventSystem *g_pGameEventSystem;

// Schema + entity system.
// g_pSchemaSystem and g_pGameResourceServiceServer come from the SDK's interfaces.lib.
class CGameEntitySystem;
extern CGameEntitySystem *g_pEntitySystem;

// Game event manager, resolved by signature (see render/center_html.cpp).
// Used to build the show_survival_respawn_status event that carries center-screen HTML.
class IGameEventManager2;
extern IGameEventManager2 *g_pGameEventManager;

// CGlobalVars accessor, only valid during an active game
#include "mmu/print.h"

inline CGlobalVars *GetGameGlobals()
{
	return mmu::GetGameGlobals();
}

// Resolve CGameEntitySystem via g_pGameResourceServiceServer + gamedata offset.
CGameEntitySystem *GameEntitySystem();

#endif // _INCLUDE_MENU_COMMON_H_
