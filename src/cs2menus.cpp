#include "cs2menus.h"
#include "common.h"

#include "config/config.h"
#include "entity/ccsplayercontroller.h"
#include "entity/cgamerules.h"
#include "gamedata.h"
#include "menu/menu_manager.h"
#include "public/ics2menus.h"
#include "render/center_html.h"

#include <cctype>
#include <cstdio>
#include <cstring>
#include <string>

#include <engine/igameeventsystem.h>
#include <interfaces/interfaces.h>
#include <iserver.h>
#include <networksystem/inetworkmessages.h>
#include <schemasystem/schemasystem.h>

SH_DECL_HOOK3_void(IServerGameDLL, GameFrame, SH_NOATTRIB, 0, bool, bool, bool);
SH_DECL_HOOK5_void(IServerGameClients, ClientDisconnect, SH_NOATTRIB, 0, CPlayerSlot, ENetworkDisconnectionReason, const char *, uint64,
				   const char *);
SH_DECL_HOOK3_void(ICvar, DispatchConCommand, SH_NOATTRIB, 0, ConCommandRef, const CCommandContext &, const CCommand &);

IServerGameDLL *g_pServerGameDLL = nullptr;
IServerGameClients *g_pGameClients = nullptr;
IVEngineServer *g_pEngine = nullptr;
ICvar *g_pICvar = nullptr;
IGameEventSystem *g_pGameEventSystem = nullptr;

CGameEntitySystem *g_pEntitySystem = nullptr;

CGameEntitySystem *GameEntitySystem()
{
	if (!g_pGameResourceServiceServer)
	{
		return nullptr;
	}
	return *reinterpret_cast<CGameEntitySystem **>(reinterpret_cast<uintptr_t>(g_pGameResourceServiceServer) + gamedata::kGameEntitySystemOffset);
}

// Cached gamerules for the HUD-flashing workaround. Re-found each map.
static CCSGameRules *s_pGameRules = nullptr;

static CCSGameRules *FindGameRules()
{
	if (s_pGameRules || !g_pEntitySystem)
	{
		return s_pGameRules;
	}

	for (int idx = 0; idx < 4096; idx++)
	{
		int chunk = idx / MAX_ENTITIES_IN_LIST;
		int off = idx % MAX_ENTITIES_IN_LIST;
		if (chunk < 0 || chunk >= MAX_ENTITY_LISTS)
		{
			break;
		}
		CEntityIdentity *pChunk = g_pEntitySystem->m_EntityList.m_pIdentityChunks[chunk];
		if (!pChunk)
		{
			continue;
		}
		CEntityInstance *inst = pChunk[off].m_pInstance;
		if (!inst)
		{
			continue;
		}
		const char *cls = inst->GetClassname();
		if (cls && strcmp(cls, "cs_gamerules") == 0)
		{
			s_pGameRules = reinterpret_cast<CCSGameRulesProxy *>(inst)->GetGameRules();
			break;
		}
	}
	return s_pGameRules;
}

// Fake CCSGameRules::m_bGameRestart to stop the center-HTML panel flashing while a
// menu is shown. Throttled ~0.5s.
static void ApplyHudFlashingFix(float curtime)
{
	if (!g_MenusConfig.menu.htmlFixFlashing)
	{
		return;
	}
	static float s_next = 0.0f;
	if (curtime < s_next)
	{
		return;
	}
	s_next = curtime + 0.5f;

	CCSGameRules *rules = FindGameRules();
	if (!rules)
	{
		return;
	}

	float restartTime;
	if (!rules->GetRestartRoundTime(restartTime))
	{
		return; // schema offset unresolved
	}

	// Only fake it while a menu is up and no real round restart is scheduled.
	bool fake = g_MenuManager.AnyHtmlMenuActive() && restartTime == 0.0f;
	rules->SetGameRestart(fake);
}

CS2MenusPlugin g_ThisPlugin;

PLUGIN_EXPOSE(CS2MenusPlugin, g_ThisPlugin);

// Public menu API: a thin forwarder onto g_MenuManager.
class CS2MenusAPI : public ICS2Menus
{
	MenuHandle CreateMenu(MenuType type, const char *title, MenuItemSelectFn onSelect) override
	{
		return g_MenuManager.CreateMenu(type, title, std::move(onSelect));
	}

	int AddItem(MenuHandle menu, const char *text, const char *info, bool disabled) override
	{
		return g_MenuManager.AddItem(menu, text, info, disabled);
	}

	void SetTitle(MenuHandle menu, const char *title) override
	{
		g_MenuManager.SetTitle(menu, title);
	}

	void SetExitButton(MenuHandle menu, bool enabled) override
	{
		g_MenuManager.SetExitButton(menu, enabled);
	}

	void SetCloseOnSelect(MenuHandle menu, bool enabled) override
	{
		g_MenuManager.SetCloseOnSelect(menu, enabled);
	}

	void SetMenuEndCallback(MenuHandle menu, MenuEndFn onEnd) override
	{
		g_MenuManager.SetMenuEndCallback(menu, std::move(onEnd));
	}

	void SetExitItem(MenuHandle menu, bool enabled) override
	{
		g_MenuManager.SetExitItem(menu, enabled);
	}

	void SetMenuKey(MenuHandle menu, MenuNavAction action, MenuButton button) override
	{
		g_MenuManager.SetMenuKey(menu, action, button);
	}

	bool DisplayMenu(MenuHandle menu, int slot, float duration) override
	{
		// GetGameGlobals is main-thread only. Off-thread passes 0,
		// the manager stamps curtime when it drains the queue on GameFrame.
		float curtime = 0.0f;
		if (g_MenuManager.OnMainThread())
		{
			CGlobalVars *globals = GetGameGlobals();
			curtime = globals ? globals->curtime : 0.0f;
		}
		return g_MenuManager.DisplayMenu(menu, slot, duration, curtime);
	}

	void CancelMenu(int slot) override
	{
		g_MenuManager.CancelMenu(slot);
	}

	bool HasMenu(int slot) override
	{
		return g_MenuManager.HasMenu(slot);
	}

	MenuHandle GetActiveMenu(int slot) override
	{
		return g_MenuManager.GetActiveMenu(slot);
	}

	MenuType GetActiveMenuType(int slot) override
	{
		return g_MenuManager.GetActiveMenuType(slot);
	}

	void DestroyMenu(MenuHandle menu) override
	{
		g_MenuManager.DestroyMenu(menu);
	}

	int GetItemCount(MenuHandle menu) override
	{
		return g_MenuManager.GetItemCount(menu);
	}

	const char *GetItemText(MenuHandle menu, int item) override
	{
		return g_MenuManager.GetItemText(menu, item);
	}

	const char *GetItemInfo(MenuHandle menu, int item) override
	{
		return g_MenuManager.GetItemInfo(menu, item);
	}
};

static CS2MenusAPI g_CS2MenusAPI;

void *CS2MenusPlugin::OnMetamodQuery(const char *iface, int *ret)
{
	if (!strcmp(iface, CS2MENUS_INTERFACE))
	{
		if (ret)
		{
			*ret = META_IFACE_OK;
		}
		return static_cast<ICS2Menus *>(&g_CS2MenusAPI);
	}

	if (ret)
	{
		*ret = META_IFACE_FAILED;
	}
	return nullptr;
}

CGlobalVars *GetGameGlobals()
{
	INetworkGameServer *pServer = g_pNetworkServerService->GetIGameServer();
	if (!pServer)
	{
		return nullptr;
	}
	return pServer->GetGlobals();
}

static MenuType ParseMenuType(const std::string &name)
{
	if (name == "html")
	{
		return MenuType::Html;
	}
	return MenuType::Chat; // "chat" and any unknown value
}

// Map a config key name to its IN_* button mask. Returns 0 for unknown names.
static uint64_t ParseNavKey(const std::string &name)
{
	if (name == "w" || name == "forward")
	{
		return in_button::Forward;
	}
	if (name == "s" || name == "back")
	{
		return in_button::Back;
	}
	if (name == "a" || name == "left" || name == "moveleft")
	{
		return in_button::MoveLeft;
	}
	if (name == "d" || name == "right" || name == "moveright")
	{
		return in_button::MoveRight;
	}
	if (name == "e" || name == "use" || name == "interact")
	{
		return in_button::Use;
	}
	if (name == "shift" || name == "speed" || name == "walk")
	{
		return in_button::Speed;
	}
	if (name == "ctrl" || name == "duck" || name == "crouch")
	{
		return in_button::Duck;
	}
	if (name == "space" || name == "jump")
	{
		return in_button::Jump;
	}
	if (name == "r" || name == "reload")
	{
		return in_button::Reload;
	}
	if (name == "mouse1" || name == "attack")
	{
		return in_button::Attack;
	}
	if (name == "mouse2" || name == "attack2")
	{
		return in_button::Attack2;
	}
	if (name == "tab" || name == "score")
	{
		return in_button::Score;
	}
	if (name == "f" || name == "inspect" || name == "lookatweapon")
	{
		return in_button::Inspect;
	}
	return 0;
}

// Uppercase footer label for a nav key name, e.g. "shift" to "SHIFT".
static std::string NavKeyLabel(const std::string &name)
{
	std::string label = name;
	for (char &c : label)
	{
		c = static_cast<char>(toupper(static_cast<unsigned char>(c)));
	}
	return label;
}

// True if the pawn button schema fields resolve. If any fails, HTML menus
// can't read input and must fall back to chat.
static bool ProbeButtonSchema()
{
	int16_t a = schema::GetOffset("CBasePlayerPawn", FNV1a("CBasePlayerPawn"), "m_pMovementServices", FNV1a("m_pMovementServices"));
	int16_t b = schema::GetOffset("CPlayer_MovementServices", FNV1a("CPlayer_MovementServices"), "m_nButtons", FNV1a("m_nButtons"));
	int16_t c = schema::GetOffset("CInButtonState", FNV1a("CInButtonState"), "m_pButtonStates", FNV1a("m_pButtonStates"));
	return a > 0 && b > 0 && c > 0;
}

// HTML menus need BOTH the event-manager signature (to render) and the button
// schema (to navigate). Re-evaluated on load and each map.
static void EvaluateHtmlAvailability()
{
	bool available = center_html::Available() && ProbeButtonSchema();
	g_MenuManager.SetHtmlAvailable(available);
	META_CONPRINTF("[CS2Menus] HTML menus %s.\n", available ? "available" : "unavailable (falling back to chat)");
}

// True for a "#rrggbb" hex color. Malformed values would silently break the
// HTML markup, so we reject them and keep the built-in default.
static bool IsValidHexColor(const std::string &s)
{
	if (s.size() != 7 || s[0] != '#')
	{
		return false;
	}
	for (size_t i = 1; i < s.size(); i++)
	{
		if (!isxdigit(static_cast<unsigned char>(s[i])))
		{
			return false;
		}
	}
	return true;
}

// Reload cfg/cs2menus/core.cfg and push the settings into the menu manager.
static void LoadAndApplyConfig()
{
	char cfgPath[512];
	snprintf(cfgPath, sizeof(cfgPath), "%s/cfg/cs2menus/core.cfg", g_SMAPI->GetBaseDir());

	if (!MENU_LoadConfig(cfgPath, g_MenusConfig))
	{
		META_CONPRINTF("[CS2Menus] No core.cfg at %s - using defaults.\n", cfgPath);
	}

	MenuManagerSettings settings;
	settings.acceptedPrefixes = g_MenusConfig.general.commandPrefix + g_MenusConfig.general.silentCommandPrefix;
	settings.itemsPerPage = g_MenusConfig.menu.itemsPerPage;
	settings.defaultType = ParseMenuType(g_MenusConfig.menu.defaultType);
	settings.defaultExitButton = g_MenusConfig.menu.exitButton;
	settings.htmlVisibleItems = g_MenusConfig.menu.htmlVisibleItems;
	settings.defaultExitItem = g_MenusConfig.menu.htmlExitItem;
	if (IsValidHexColor(g_MenusConfig.menu.htmlNavColor))
	{
		settings.navColor = g_MenusConfig.menu.htmlNavColor;
	}
	if (IsValidHexColor(g_MenusConfig.menu.htmlFooterColor))
	{
		settings.footerColor = g_MenusConfig.menu.htmlFooterColor;
	}
	if (IsValidHexColor(g_MenusConfig.menu.htmlDisabledColor))
	{
		settings.disabledColor = g_MenusConfig.menu.htmlDisabledColor;
	}

	// HTML nav keys. "none"/"off"/blank disables the action (mask 0),
	// an unrecognized name leaves the default binding untouched.
	auto applyNav = [](const std::string &name, uint64_t &mask, std::string &label)
	{
		if (name == "none" || name == "off" || name.empty())
		{
			mask = 0;
			label = "";
			return;
		}
		uint64_t m = ParseNavKey(name);
		if (m != 0)
		{
			mask = m;
			label = NavKeyLabel(name);
		}
	};
	applyNav(g_MenusConfig.menu.navUp, settings.keyUp, settings.keyUpLabel);
	applyNav(g_MenusConfig.menu.navDown, settings.keyDown, settings.keyDownLabel);
	applyNav(g_MenusConfig.menu.navSelect, settings.keySelect, settings.keySelectLabel);
	applyNav(g_MenusConfig.menu.navBack, settings.keyBack, settings.keyBackLabel);

	g_MenuManager.Configure(settings);
}

bool CS2MenusPlugin::Load(PluginId id, ISmmAPI *ismm, char *error, size_t maxlen, bool late)
{
	PLUGIN_SAVEVARS();

	// Load runs on the game thread.
	// Record it so the menu API can tell main-thread callers from worker-thread callers.
	g_MenuManager.SetMainThread();

	GET_V_IFACE_CURRENT(GetEngineFactory, g_pEngine, IVEngineServer, INTERFACEVERSION_VENGINESERVER);
	GET_V_IFACE_CURRENT(GetEngineFactory, g_pICvar, ICvar, CVAR_INTERFACE_VERSION);
	GET_V_IFACE_ANY(GetServerFactory, g_pServerGameDLL, IServerGameDLL, INTERFACEVERSION_SERVERGAMEDLL);
	GET_V_IFACE_ANY(GetServerFactory, g_pGameClients, IServerGameClients, INTERFACEVERSION_SERVERGAMECLIENTS);
	GET_V_IFACE_ANY(GetEngineFactory, g_pNetworkServerService, INetworkServerService, NETWORKSERVERSERVICE_INTERFACE_VERSION);
	GET_V_IFACE_ANY(GetEngineFactory, g_pGameEventSystem, IGameEventSystem, GAMEEVENTSYSTEM_INTERFACE_VERSION);
	GET_V_IFACE_ANY(GetEngineFactory, g_pNetworkMessages, INetworkMessages, NETWORKMESSAGES_INTERFACE_VERSION);
	GET_V_IFACE_ANY(GetEngineFactory, g_pSchemaSystem, ISchemaSystem, SCHEMASYSTEM_INTERFACE_VERSION);
	GET_V_IFACE_ANY(GetEngineFactory, g_pGameResourceServiceServer, IGameResourceService, GAMERESOURCESERVICESERVER_INTERFACE_VERSION);

	g_SMAPI->AddListener(this, this);

	SH_ADD_HOOK(IServerGameDLL, GameFrame, g_pServerGameDLL, SH_MEMBER(this, &CS2MenusPlugin::Hook_GameFrame), true);
	SH_ADD_HOOK(IServerGameClients, ClientDisconnect, g_pGameClients, SH_MEMBER(this, &CS2MenusPlugin::Hook_ClientDisconnect), true);
	SH_ADD_HOOK(ICvar, DispatchConCommand, g_pICvar, SH_MEMBER(this, &CS2MenusPlugin::Hook_DispatchConCommand), false);

	g_pCVar = g_pICvar;
	META_CONVAR_REGISTER(FCVAR_RELEASE | FCVAR_CLIENT_CAN_EXECUTE | FCVAR_GAMEDLL);

	// Resolve the game event manager for HTML menus. Chat menus work if this fails.
	center_html::Init();

	// On a late load the map is already running, so grab the entity system now.
	if (late)
	{
		g_pEntitySystem = GameEntitySystem();
	}

	EvaluateHtmlAvailability();
	LoadAndApplyConfig();

	META_CONPRINTF("[CS2Menus] Plugin loaded.\n");
	return true;
}

void CS2MenusPlugin::OnLevelInit(char const * /*pMapName*/, char const * /*pMapEntities*/, char const * /*pOldLevel*/, char const * /*pLandmarkName*/,
								 bool /*loadGame*/, bool /*background*/)
{
	g_pEntitySystem = GameEntitySystem();
	s_pGameRules = nullptr; // gamerules proxy is recreated each map, re-find lazily

	// Retry the event-manager sig scan in case it missed at load
	// (server module not yet mapped), then re-probe. Init is a no-op once resolved.
	center_html::Init();

	// Schema is reliably ready by now.
	// Re-check in case the load-time probe was early.
	EvaluateHtmlAvailability();

	// Pick up config edits on each map change.
	LoadAndApplyConfig();
}

void CS2MenusPlugin::OnLevelShutdown()
{
	// Entity system and gamerules are freed on level end. Drop our cached pointers
	// so GameFrame can't touch freed memory before OnLevelInit re-acquires them.
	g_pEntitySystem = nullptr;
	s_pGameRules = nullptr;
}

bool CS2MenusPlugin::Unload(char *error, size_t maxlen)
{
	SH_REMOVE_HOOK(IServerGameDLL, GameFrame, g_pServerGameDLL, SH_MEMBER(this, &CS2MenusPlugin::Hook_GameFrame), true);
	SH_REMOVE_HOOK(IServerGameClients, ClientDisconnect, g_pGameClients, SH_MEMBER(this, &CS2MenusPlugin::Hook_ClientDisconnect), true);
	SH_REMOVE_HOOK(ICvar, DispatchConCommand, g_pICvar, SH_MEMBER(this, &CS2MenusPlugin::Hook_DispatchConCommand), false);

	// Drop all menus + displays without firing callbacks into consumer plugins.
	g_MenuManager.Shutdown();

	return true;
}

void CS2MenusPlugin::Hook_GameFrame(bool /*simulating*/, bool /*bFirstTick*/, bool /*bLastTick*/)
{
	CGlobalVars *globals = GetGameGlobals();
	if (!globals)
	{
		RETURN_META(MRES_IGNORED);
	}

	float curtime = globals->curtime;

	if (g_pEntitySystem)
	{
		int maxClients = globals->maxClients;
		for (int slot = 0; slot < maxClients && slot <= MAXPLAYERS; slot++)
		{
			if (!g_MenuManager.WantsButtonInput(slot))
			{
				continue;
			}
			CCSPlayerController *controller = CCSPlayerController::FromSlot(slot);
			if (!controller)
			{
				continue;
			}
			CCSPlayerPawn *pawn = controller->GetPawn();
			if (!pawn)
			{
				continue;
			}
			g_MenuManager.PollButtons(slot, pawn->GetHeldButtons(), curtime);
		}
	}

	g_MenuManager.Tick(curtime);
	ApplyHudFlashingFix(curtime);
	RETURN_META(MRES_IGNORED);
}

void CS2MenusPlugin::Hook_ClientDisconnect(CPlayerSlot slot, ENetworkDisconnectionReason /*reason*/, const char * /*pszName*/, uint64 /*xuid*/,
										   const char * /*pszNetworkID*/)
{
	g_MenuManager.OnPlayerDisconnect(slot.Get());
	RETURN_META(MRES_IGNORED);
}

void CS2MenusPlugin::Hook_DispatchConCommand(ConCommandRef cmd, const CCommandContext &ctx, const CCommand &args)
{
	const char *cmdName = args.Arg(0);
	if (!cmdName)
	{
		RETURN_META(MRES_IGNORED);
	}

	bool isSay = (strcmp(cmdName, "say") == 0 || strcmp(cmdName, "say_team") == 0);
	if (!isSay)
	{
		RETURN_META(MRES_IGNORED);
	}

	int slot = ctx.GetPlayerSlot().Get();
	if (slot < 0 || slot > MAXPLAYERS)
	{
		RETURN_META(MRES_IGNORED);
	}

	if (!g_MenuManager.HasMenu(slot))
	{
		RETURN_META(MRES_IGNORED);
	}

	const char *rawMsg = args.ArgS();
	if (!rawMsg || !rawMsg[0])
	{
		RETURN_META(MRES_IGNORED);
	}

	// ProcessInput strips the outer quotes CS2 wraps around the say message.
	CGlobalVars *globals = GetGameGlobals();
	float curtime = globals ? globals->curtime : 0.0f;

	if (g_MenuManager.ProcessInput(slot, rawMsg, curtime))
	{
		// Suppress the chat line so the number doesn't show.
		RETURN_META(MRES_SUPERCEDE);
	}

	RETURN_META(MRES_IGNORED);
}
