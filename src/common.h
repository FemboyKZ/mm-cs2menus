#ifndef _INCLUDE_MENU_COMMON_H_
#define _INCLUDE_MENU_COMMON_H_

#include "utils/chat_colors.h"
#include "sdk/plugin_globals.h"

#include <ISmmPlugin.h>
#include <igameevents.h>
#include <engine2/inetworkserverservice.h>
#include <engine2/networkgameserverbase.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

// Plugin-specific engine interfaces. Shared ones live in sdk/plugin_globals.h.
extern INetworkServerService *g_pNetworkServerService;

// Schema + entity system.
// g_pSchemaSystem and g_pGameResourceServiceServer come from the SDK's interfaces.lib.
class CGameEntitySystem;
extern CGameEntitySystem *g_pEntitySystem;

// Game event manager, resolved by signature (see render/center_html.cpp).
// Used to build the show_survival_respawn_status event that carries center-screen HTML.
class IGameEventManager2;
extern IGameEventManager2 *g_pGameEventManager;

// CGlobalVars accessor, only valid during an active game
#include "game/print.h"

// Clock for every menu time value, callable from any thread.
// Not globals->curtime, which restarts each map, so a menu open across a map change would wait on the old map's times.
// Counted from first use so the float keeps its precision over a long uptime.
inline float MenuNow()
{
	static const double start = Plat_FloatTime();
	return static_cast<float>(Plat_FloatTime() - start);
}

#endif // _INCLUDE_MENU_COMMON_H_
