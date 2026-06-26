#include "cs2menus.h"
#include "common.h"

#include "config/config.h"
#include "entity/ccsplayercontroller.h"
#include "entity/cgamerules.h"
#include "gamedata.h"
#include "lang/translations.h"
#include "menu/menu_manager.h"
#include "public/ics2menus.h"
#include "render/center_html.h"

#include "vendor/ClientCvarValue/public/iclientcvarvalue.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
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

static IClientCvarValue *g_pClientCvarValue = nullptr;

// Language key for the player in `slot`, mapped from their cl_language.
// Empty string when unavailable, which makes Translate use the default language.
static std::string SlotLanguage(int slot)
{
	const char *raw = g_pClientCvarValue ? g_pClientCvarValue->GetClientLanguage(CPlayerSlot(slot)) : nullptr;
	return g_Translations.MapClientLanguage(raw);
}

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

	void SetMenuLabel(MenuHandle menu, MenuLabel label, const char *text) override
	{
		g_MenuManager.SetMenuLabel(menu, label, text);
	}

	const char *GetMenuLabel(MenuHandle menu, MenuLabel label) override
	{
		return g_MenuManager.GetMenuLabel(menu, label);
	}

	void SetMenuStyle(MenuHandle menu, MenuStyle field, const char *value) override
	{
		g_MenuManager.SetMenuStyle(menu, field, value);
	}

	const char *GetMenuStyle(MenuHandle menu, MenuStyle field) override
	{
		return g_MenuManager.GetMenuStyle(menu, field);
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

	void SetItemText(MenuHandle menu, int item, const char *text) override
	{
		g_MenuManager.SetItemText(menu, item, text);
	}

	void SetItemDisabled(MenuHandle menu, int item, bool disabled) override
	{
		g_MenuManager.SetItemDisabled(menu, item, disabled);
	}

	void RemoveItem(MenuHandle menu, int item) override
	{
		g_MenuManager.RemoveItem(menu, item);
	}

	void RemoveAllItems(MenuHandle menu) override
	{
		g_MenuManager.RemoveAllItems(menu);
	}

	void SetStartItem(MenuHandle menu, int item) override
	{
		g_MenuManager.SetStartItem(menu, item);
	}

	void DisplayMenuToAll(MenuHandle menu, float duration) override
	{
		// Enumerating players needs main-thread entity access, so run there.
		g_MenuManager.RunOnMainThread(
			[menu, duration]
			{
				CGlobalVars *globals = GetGameGlobals();
				if (!globals)
				{
					return;
				}
				int maxClients = globals->maxClients;
				float curtime = globals->curtime;
				for (int slot = 0; slot < maxClients && slot <= MAXPLAYERS; slot++)
				{
					if (!CCSPlayerController::FromSlot(slot))
					{
						continue;
					}
					g_MenuManager.DisplayMenu(menu, slot, duration, curtime);
				}
			});
	}

	int GetSelectedItem(int slot) override
	{
		return g_MenuManager.GetSelectedItem(slot);
	}

	void SetItemInfo(MenuHandle menu, int item, const char *info) override
	{
		g_MenuManager.SetItemInfo(menu, item, info);
	}

	bool GetItemDisabled(MenuHandle menu, int item) override
	{
		return g_MenuManager.GetItemDisabled(menu, item);
	}

	void SetItemRaw(MenuHandle menu, int item, bool raw) override
	{
		g_MenuManager.SetItemRaw(menu, item, raw);
	}

	bool GetItemRaw(MenuHandle menu, int item) override
	{
		return g_MenuManager.GetItemRaw(menu, item);
	}

	void SetItemIcon(MenuHandle menu, int item, const char *url) override
	{
		g_MenuManager.SetItemIcon(menu, item, url);
	}

	const char *GetItemIcon(MenuHandle menu, int item) override
	{
		return g_MenuManager.GetItemIcon(menu, item);
	}

	int GetStartItem(MenuHandle menu) override
	{
		return g_MenuManager.GetStartItem(menu);
	}

	int AddSubMenu(MenuHandle parent, const char *text, MenuHandle child, const char *info) override
	{
		return g_MenuManager.AddSubMenu(parent, text, child, info);
	}

	void SetExternalBusy(int slot, bool busy) override
	{
		g_MenuManager.SetExternalBusy(slot, busy);
	}

	bool GetExternalBusy(int slot) override
	{
		return g_MenuManager.GetExternalBusy(slot);
	}
};

static CS2MenusAPI g_CS2MenusAPI;

// Used by the flat C ABI facade (src/bridge/capi.cpp) so it routes through the
// same interface instance (curtime stamping + off-thread queueing) as MetaFactory consumers.
ICS2Menus *Cs2Menus_GetLocalAPI()
{
	return &g_CS2MenusAPI;
}

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

// Resolve a chat color name to its client control byte (see CHAT_COLOR_* in common.h).
// Chat has only the fixed client palette, so colors are names, not hex.
// Names match common.h. Unknown names return `fallback` so a typo keeps the built-in color.
static std::string ChatColorByte(const std::string &name, const char *fallback)
{
	struct Entry
	{
		const char *name;
		const char *code;
	};

	static const Entry kColors[] = {
		{"default", CHAT_COLOR_DEFAULT}, {"darkred", CHAT_COLOR_DARKRED},   {"purple", CHAT_COLOR_PURPLE},     {"green", CHAT_COLOR_GREEN},
		{"olive", CHAT_COLOR_OLIVE},     {"lime", CHAT_COLOR_LIME},         {"red", CHAT_COLOR_RED},           {"grey", CHAT_COLOR_GREY},
		{"yellow", CHAT_COLOR_YELLOW},   {"bluegrey", CHAT_COLOR_BLUEGREY}, {"blue", CHAT_COLOR_BLUE},         {"darkblue", CHAT_COLOR_DARKBLUE},
		{"grey2", CHAT_COLOR_GREY2},     {"orchid", CHAT_COLOR_ORCHID},     {"lightred", CHAT_COLOR_LIGHTRED}, {"gold", CHAT_COLOR_GOLD},
	};
	for (const Entry &e : kColors)
	{
		if (name == e.name)
		{
			return e.code;
		}
	}
	return fallback;
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
	const MenuDefaultsCfg &m = g_MenusConfig.menu;
	settings.acceptedPrefixes = g_MenusConfig.general.commandPrefix + g_MenusConfig.general.silentCommandPrefix;
	settings.itemsPerPage = g_MenusConfig.menu.itemsPerPage;
	settings.defaultType = ParseMenuType(g_MenusConfig.menu.defaultType);
	settings.defaultExitButton = g_MenusConfig.menu.exitButton;

	// Chat styling: resolve color names to control bytes, copy decoration as-is.
	settings.chatTitleColor = ChatColorByte(m.chatTitleColor, CHAT_COLOR_GREEN);
	settings.chatPageColor = ChatColorByte(m.chatPageColor, CHAT_COLOR_DEFAULT);
	settings.chatItemColor = ChatColorByte(m.chatItemColor, CHAT_COLOR_DEFAULT);
	settings.chatDisabledColor = ChatColorByte(m.chatDisabledColor, CHAT_COLOR_GREY);
	settings.chatArrowColor = ChatColorByte(m.chatArrowColor, CHAT_COLOR_GREEN);
	settings.chatHeaderColor = ChatColorByte(m.chatHeaderColor, CHAT_COLOR_DEFAULT);
	settings.chatTitlePrefix = m.chatTitlePrefix;
	settings.chatTitleSuffix = m.chatTitleSuffix;
	settings.chatNumberPrefix = m.chatNumberPrefix;
	settings.chatNumberSuffix = m.chatNumberSuffix;
	settings.chatDisabledPrefix = m.chatDisabledPrefix;
	settings.chatArrow = m.chatArrow;
	settings.chatPagePrefix = m.chatPagePrefix;
	settings.chatPageSuffix = m.chatPageSuffix;
	settings.chatShowPage = m.chatShowPage;
	settings.chatHeader = m.chatHeader;
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
	// Size tokens accepted by the renderer. An unknown value keeps the built-in default.
	auto isSizeToken = [](const std::string &t)
	{ return t == "xs" || t == "s" || t == "sm" || t == "m" || t == "ml" || t == "l" || t == "xl" || t == "xxl" || t == "xxxl"; };
	if (IsValidHexColor(g_MenusConfig.menu.htmlTitleColor))
	{
		settings.titleColor = g_MenusConfig.menu.htmlTitleColor;
	}
	if (IsValidHexColor(g_MenusConfig.menu.htmlItemColor))
	{
		settings.itemColor = g_MenusConfig.menu.htmlItemColor;
	}
	// Font face is an arbitrary Panorama class, accepted as-is (empty keeps the game default).
	settings.fontFace = g_MenusConfig.menu.htmlFontFace;
	settings.marker = g_MenusConfig.menu.htmlMarker;
	if (IsValidHexColor(g_MenusConfig.menu.htmlCounterColor))
	{
		settings.counterColor = g_MenusConfig.menu.htmlCounterColor;
	}
	if (isSizeToken(g_MenusConfig.menu.htmlFooterSize))
	{
		settings.footerSize = g_MenusConfig.menu.htmlFooterSize;
	}
	settings.showCounter = g_MenusConfig.menu.htmlShowCounter;
	settings.showFooter = g_MenusConfig.menu.htmlShowFooter;
	settings.submenuSuffix = g_MenusConfig.menu.htmlSubmenuSuffix;
	settings.footerSeparator = g_MenusConfig.menu.htmlFooterSeparator;
	settings.counterPrefix = g_MenusConfig.menu.htmlCounterPrefix;
	settings.counterSuffix = g_MenusConfig.menu.htmlCounterSuffix;
	settings.highlightText = g_MenusConfig.menu.htmlHighlightText;
	// Resend cadence: keep sane and keepAlive strictly below the decay duration, else the panel blinks.
	if (g_MenusConfig.menu.htmlDurationSecs >= 1)
	{
		settings.htmlDurationSecs = g_MenusConfig.menu.htmlDurationSecs;
	}
	if (g_MenusConfig.menu.htmlRefreshInterval > 0.0f)
	{
		settings.htmlRefreshInterval = g_MenusConfig.menu.htmlRefreshInterval;
	}
	if (g_MenusConfig.menu.htmlKeepAlive > 0.0f && g_MenusConfig.menu.htmlKeepAlive < settings.htmlDurationSecs)
	{
		settings.htmlKeepAlive = g_MenusConfig.menu.htmlKeepAlive;
	}
	if (isSizeToken(g_MenusConfig.menu.htmlTitleSize))
	{
		settings.titleSize = g_MenusConfig.menu.htmlTitleSize;
	}
	if (isSizeToken(g_MenusConfig.menu.htmlItemSize))
	{
		settings.itemSize = g_MenusConfig.menu.htmlItemSize;
	}
	if (g_MenusConfig.menu.htmlAlign == "left" || g_MenusConfig.menu.htmlAlign == "center" || g_MenusConfig.menu.htmlAlign == "right")
	{
		settings.align = g_MenusConfig.menu.htmlAlign;
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

	// Label translations. Re-acquire ClientCvarValue (optional, may load after us),
	// reload the phrase files, and wire per-viewer language resolution.
	g_pClientCvarValue = static_cast<IClientCvarValue *>(g_SMAPI->MetaFactory(CLIENTCVARVALUE_INTERFACE, nullptr, nullptr));
	g_Translations.Load(g_SMAPI->GetBaseDir());
	g_Translations.SetDefaultLanguage(g_MenusConfig.menu.defaultLanguage);
	g_MenuManager.SetLanguageResolver([](int slot) { return SlotLanguage(slot); });
}

// Server console / rcon command to reapply core.cfg without waiting for a map change.
CON_COMMAND_F(cs2menus_reload, "Reload cs2menus core.cfg and re-probe HTML availability.", FCVAR_RELEASE | FCVAR_GAMEDLL)
{
	LoadAndApplyConfig();
	EvaluateHtmlAvailability();
	META_CONPRINTF("[CS2Menus] Config reloaded.\n");
}

CON_COMMAND_F(cs2menus_version, "Print the cs2menus plugin and menu-API interface versions.", FCVAR_RELEASE | FCVAR_GAMEDLL)
{
	META_CONPRINTF("[CS2Menus] Plugin version %s, interface %s.\n", PLUGIN_FULL_VERSION, CS2MENUS_INTERFACE);
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
			// Use the controlled pawn so dead/spectating players can still navigate.
			CBasePlayerPawn *pawn = controller->GetInputPawn();
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

// Map a bare command body to a nav action. "menu_close" also answers to exit/back.
static bool MatchMenuNavBody(const char *body, MenuNavAction &action)
{
	if (!strcmp(body, "menu_up"))
	{
		action = MenuNavAction::Up;
		return true;
	}
	if (!strcmp(body, "menu_down"))
	{
		action = MenuNavAction::Down;
		return true;
	}
	if (!strcmp(body, "menu_select"))
	{
		action = MenuNavAction::Select;
		return true;
	}
	if (!strcmp(body, "menu_close") || !strcmp(body, "menu_exit") || !strcmp(body, "menu_back"))
	{
		action = MenuNavAction::Back;
		return true;
	}
	return false;
}

// Parse a say message like "!menu_up", "/menu_close" or "!menu_select 3" into a nav
// action plus an optional number (-1 if none, used by menu_select on chat menus).
// `silent` is set when the silent prefix matched (only then is the chat line hidden).
// Requires the configured chat or silent prefix, returns false otherwise.
static bool ParseChatMenuNav(const char *rawMsg, MenuNavAction &action, int &number, bool &silent)
{
	number = -1;
	silent = false;
	std::string msg = rawMsg ? rawMsg : "";
	// CS2 wraps the say argument in quotes.
	if (msg.size() >= 2 && msg.front() == '"' && msg.back() == '"')
	{
		msg = msg.substr(1, msg.size() - 2);
	}
	size_t a = msg.find_first_not_of(" \t");
	size_t b = msg.find_last_not_of(" \t");
	if (a == std::string::npos)
	{
		return false;
	}
	msg = msg.substr(a, b - a + 1);

	const std::string &p1 = g_MenusConfig.general.commandPrefix;
	const std::string &p2 = g_MenusConfig.general.silentCommandPrefix;
	std::string body;
	if (!p1.empty() && msg.rfind(p1, 0) == 0)
	{
		body = msg.substr(p1.size());
	}
	else if (!p2.empty() && msg.rfind(p2, 0) == 0)
	{
		body = msg.substr(p2.size());
		silent = true; // only the silent prefix hides the chat line
	}
	else
	{
		return false;
	}

	// Split "menu_select 3" into command + optional argument.
	std::string cmd = body;
	std::string arg;
	size_t sp = body.find_first_of(" \t");
	if (sp != std::string::npos)
	{
		cmd = body.substr(0, sp);
		size_t argStart = body.find_first_not_of(" \t", sp);
		if (argStart != std::string::npos)
		{
			arg = body.substr(argStart);
		}
	}
	for (char &c : cmd)
	{
		c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
	}
	if (!MatchMenuNavBody(cmd.c_str(), action))
	{
		return false;
	}
	if (!arg.empty())
	{
		number = atoi(arg.c_str());
	}
	return true;
}

// Client console backup for menu navigation.
// Server console has no player slot and no menu, so it returns.
static void RunMenuNavCommand(const CCommandContext &context, MenuNavAction action)
{
	int slot = context.GetPlayerSlot().Get();
	if (slot < 0 || slot > MAXPLAYERS)
	{
		return;
	}
	CGlobalVars *globals = GetGameGlobals();
	g_MenuManager.CommandNav(slot, action, globals ? globals->curtime : 0.0f);
}

CON_COMMAND_F(mm_menu_up, "Move the open menu's cursor up.", FCVAR_CLIENT_CAN_EXECUTE | FCVAR_GAMEDLL)
{
	RunMenuNavCommand(context, MenuNavAction::Up);
}

CON_COMMAND_F(mm_menu_down, "Move the open menu's cursor down.", FCVAR_CLIENT_CAN_EXECUTE | FCVAR_GAMEDLL)
{
	RunMenuNavCommand(context, MenuNavAction::Down);
}

// "mm_menu_select" selects the highlighted HTML row, or "mm_menu_select 3" picks chat entry 3.
// Server console returns.
CON_COMMAND_F(mm_menu_select, "Select a menu item: no arg = highlighted row, a number = that chat entry.", FCVAR_CLIENT_CAN_EXECUTE | FCVAR_GAMEDLL)
{
	int slot = context.GetPlayerSlot().Get();
	if (slot < 0 || slot > MAXPLAYERS)
	{
		return;
	}
	CGlobalVars *globals = GetGameGlobals();
	float curtime = globals ? globals->curtime : 0.0f;
	if (args.ArgC() > 1)
	{
		g_MenuManager.CommandSelectNumber(slot, atoi(args.Arg(1)), curtime);
	}
	else
	{
		g_MenuManager.CommandNav(slot, MenuNavAction::Select, curtime);
	}
}

CON_COMMAND_F(mm_menu_close, "Close / exit the open menu.", FCVAR_CLIENT_CAN_EXECUTE | FCVAR_GAMEDLL)
{
	RunMenuNavCommand(context, MenuNavAction::Back);
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

	// Backup nav via chat: "!menu_up" / "/menu_close" / "!menu_select 3".
	// The silent prefix hides the chat line, the normal prefix leaves it visible.
	MenuNavAction navAction;
	int navNumber;
	bool navSilent;
	if (ParseChatMenuNav(rawMsg, navAction, navNumber, navSilent))
	{
		if (navAction == MenuNavAction::Select && navNumber >= 0)
		{
			g_MenuManager.CommandSelectNumber(slot, navNumber, curtime);
		}
		else
		{
			g_MenuManager.CommandNav(slot, navAction, curtime);
		}
		RETURN_META(navSilent ? MRES_SUPERCEDE : MRES_IGNORED);
	}

	if (g_MenuManager.ProcessInput(slot, rawMsg, curtime))
	{
		// Suppress the chat line so the number doesn't show.
		RETURN_META(MRES_SUPERCEDE);
	}

	RETURN_META(MRES_IGNORED);
}
