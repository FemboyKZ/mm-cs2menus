#ifndef _INCLUDE_MENU_COMMON_H_
#define _INCLUDE_MENU_COMMON_H_

#include <ISmmPlugin.h>
#include <igameevents.h>
#include <iserver.h>
#include <sh_vector.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

#define MAXPLAYERS 64

// A player slot in the valid 0..MAXPLAYERS range.
inline bool ValidSlot(int slot)
{
	return slot >= 0 && slot <= MAXPLAYERS;
}

// CS2 chat color codes
#define CHAT_COLOR_DEFAULT  "\x01"
#define CHAT_COLOR_DARKRED  "\x02"
#define CHAT_COLOR_PURPLE   "\x03"
#define CHAT_COLOR_GREEN    "\x04"
#define CHAT_COLOR_OLIVE    "\x05"
#define CHAT_COLOR_LIME     "\x06"
#define CHAT_COLOR_RED      "\x07"
#define CHAT_COLOR_GREY     "\x08"
#define CHAT_COLOR_YELLOW   "\x09"
#define CHAT_COLOR_BLUEGREY "\x0A"
#define CHAT_COLOR_BLUE     "\x0B"
#define CHAT_COLOR_DARKBLUE "\x0C"
#define CHAT_COLOR_GREY2    "\x0D"
#define CHAT_COLOR_ORCHID   "\x0E"
#define CHAT_COLOR_LIGHTRED "\x0F"
#define CHAT_COLOR_GOLD     "\x10"

// Engine interface declarations
extern IServerGameDLL *g_pServerGameDLL;
extern IServerGameClients *g_pGameClients;
extern IVEngineServer *g_pEngine;
extern ICvar *g_pICvar;
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

// Metamod globals
extern ISmmAPI *g_SMAPI;
extern ISmmPlugin *g_PLAPI;
extern PluginId g_PLID;
extern SourceHook::ISourceHook *g_SHPtr;

// CGlobalVars accessor, only valid during an active game
CGlobalVars *GetGameGlobals();

// Resolve CGameEntitySystem via g_pGameResourceServiceServer + gamedata offset.
CGameEntitySystem *GameEntitySystem();

#endif // _INCLUDE_MENU_COMMON_H_
