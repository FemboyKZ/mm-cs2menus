#include "cs2menus.h"
#include "common.h"

#include "admin/menu_admin_bridge.h"

#include "config/config.h"
#include "db/prefs_db.h"
#include "mmu/entity/ccsplayercontroller.h"
#include "entity/cgamerules.h"
#include "gamedata.h"
#include "lang/translations.h"
#include "menu/key_table.h"
#include "menu/menu_manager.h"
#include "public/ics2menus.h"
#include "render/center_html.h"
#include "utils/html_style.h"
#include "utils/print_utils.h"
#include "mmu/log.h"
#include "mmu/str_utils.h"

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
SH_DECL_HOOK6_void(IServerGameClients, OnClientConnected, SH_NOATTRIB, 0, CPlayerSlot, const char *, uint64, const char *, const char *, bool);
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
	// --- Lifetime ---

	MenuHandle CreateMenu(MenuType type, const char *title, MenuItemSelectFn onSelect) override
	{
		return g_MenuManager.CreateMenu(type, title, std::move(onSelect));
	}

	void DestroyMenu(MenuHandle menu) override
	{
		g_MenuManager.DestroyMenu(menu);
	}

	bool IsValidMenu(MenuHandle menu) override
	{
		return g_MenuManager.IsValidMenu(menu);
	}

	// --- Menu properties ---

	void SetTitle(MenuHandle menu, const char *title) override
	{
		g_MenuManager.SetTitle(menu, title);
	}

	const char *GetTitle(MenuHandle menu) override
	{
		return g_MenuManager.GetTitle(menu);
	}

	MenuType GetMenuType(MenuHandle menu) override
	{
		return g_MenuManager.GetMenuType(menu);
	}

	void SetExitButton(MenuHandle menu, bool enabled) override
	{
		g_MenuManager.SetExitButton(menu, enabled);
	}

	bool GetExitButton(MenuHandle menu) override
	{
		return g_MenuManager.GetExitButton(menu);
	}

	void SetCloseOnSelect(MenuHandle menu, bool enabled) override
	{
		g_MenuManager.SetCloseOnSelect(menu, enabled);
	}

	bool GetCloseOnSelect(MenuHandle menu) override
	{
		return g_MenuManager.GetCloseOnSelect(menu);
	}

	void SetExitItem(MenuHandle menu, bool enabled) override
	{
		g_MenuManager.SetExitItem(menu, enabled);
	}

	bool GetExitItem(MenuHandle menu) override
	{
		return g_MenuManager.GetExitItem(menu);
	}

	void SetMenuForceType(MenuHandle menu, bool force) override
	{
		g_MenuManager.SetMenuForceType(menu, force);
	}

	bool GetMenuForceType(MenuHandle menu) override
	{
		return g_MenuManager.GetMenuForceType(menu);
	}

	void SetStartItem(MenuHandle menu, int item) override
	{
		g_MenuManager.SetStartItem(menu, item);
	}

	int GetStartItem(MenuHandle menu) override
	{
		return g_MenuManager.GetStartItem(menu);
	}

	void SetMenuEndCallback(MenuHandle menu, MenuEndFn onEnd) override
	{
		g_MenuManager.SetMenuEndCallback(menu, std::move(onEnd));
	}

	void SetMenuKey(MenuHandle menu, MenuNavAction action, MenuButton button) override
	{
		g_MenuManager.SetMenuKey(menu, action, button);
	}

	MenuButton GetMenuKey(MenuHandle menu, MenuNavAction action) override
	{
		return g_MenuManager.GetMenuKey(menu, action);
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

	// --- Items ---

	int AddItem(MenuHandle menu, const char *text, const char *info, bool disabled) override
	{
		return g_MenuManager.AddItem(menu, text, info, disabled);
	}

	int InsertItem(MenuHandle menu, int pos, const char *text, const char *info, bool disabled) override
	{
		return g_MenuManager.InsertItem(menu, pos, text, info, disabled);
	}

	int AddSubMenu(MenuHandle parent, const char *text, MenuHandle child, const char *info) override
	{
		return g_MenuManager.AddSubMenu(parent, text, child, info);
	}

	void RemoveItem(MenuHandle menu, int item) override
	{
		g_MenuManager.RemoveItem(menu, item);
	}

	void RemoveAllItems(MenuHandle menu) override
	{
		g_MenuManager.RemoveAllItems(menu);
	}

	int GetItemCount(MenuHandle menu) override
	{
		return g_MenuManager.GetItemCount(menu);
	}

	void SetItemText(MenuHandle menu, int item, const char *text) override
	{
		g_MenuManager.SetItemText(menu, item, text);
	}

	const char *GetItemText(MenuHandle menu, int item) override
	{
		return g_MenuManager.GetItemText(menu, item);
	}

	void SetItemInfo(MenuHandle menu, int item, const char *info) override
	{
		g_MenuManager.SetItemInfo(menu, item, info);
	}

	const char *GetItemInfo(MenuHandle menu, int item) override
	{
		return g_MenuManager.GetItemInfo(menu, item);
	}

	void SetItemDisabled(MenuHandle menu, int item, bool disabled) override
	{
		g_MenuManager.SetItemDisabled(menu, item, disabled);
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

	void SetItemSubmenu(MenuHandle menu, int item, MenuHandle child) override
	{
		g_MenuManager.SetItemSubmenu(menu, item, child);
	}

	MenuHandle GetItemSubmenu(MenuHandle menu, int item) override
	{
		return g_MenuManager.GetItemSubmenu(menu, item);
	}

	// --- Display ---

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

	int GetSelectedItem(int slot) override
	{
		return g_MenuManager.GetSelectedItem(slot);
	}

	// --- Host coordination ---

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

// Map a config key name (canonical or alias) to its IN_* button mask. Returns 0 for unknown names.
static uint64_t ParseNavKey(const std::string &name)
{
	const keys::KeyDef *k = keys::FindByName(name);
	return k ? k->mask : 0;
}

// Footer label for a nav key name. Resolves aliases to the canonical label
// (e.g. "speed"/"walk" to "SHIFT", "forward" to "W") so config, player-pref and consumer SetMenuKey paths all show the same hint.
// Unknown names uppercase as-is.
static std::string NavKeyLabel(const std::string &name)
{
	if (const keys::KeyDef *k = keys::FindByName(name))
	{
		return k->label;
	}
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
	MMU_LOG_INFO("HTML menus %s.\n", available ? "available" : "unavailable (falling back to chat)");
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
		MMU_LOG_INFO("No core.cfg at %s - using defaults.\n", cfgPath);
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
	settings.chatTitleFormat = m.chatTitleFormat;
	settings.chatNumberFormat = m.chatNumberFormat;
	settings.chatDisabledFormat = m.chatDisabledFormat;
	settings.chatArrow = m.chatArrow;
	settings.chatPageFormat = m.chatPageFormat;
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
	if (html_style::IsSizeToken(g_MenusConfig.menu.htmlFooterSize))
	{
		settings.footerSize = g_MenusConfig.menu.htmlFooterSize;
	}
	if (html_style::IsSizeToken(g_MenusConfig.menu.htmlCounterSize))
	{
		settings.counterSize = g_MenusConfig.menu.htmlCounterSize;
	}
	settings.showCounter = g_MenusConfig.menu.htmlShowCounter;
	settings.showFooter = g_MenusConfig.menu.htmlShowFooter;
	settings.submenuSuffix = g_MenusConfig.menu.htmlSubmenuSuffix;
	settings.footerSeparator = g_MenusConfig.menu.htmlFooterSeparator;
	settings.counterFormat = g_MenusConfig.menu.htmlCounterFormat;
	settings.footerHintFormat = g_MenusConfig.menu.htmlFooterHintFormat;
	settings.footerRangeFormat = g_MenusConfig.menu.htmlFooterRangeFormat;
	settings.highlightText = g_MenusConfig.menu.htmlHighlightText;
	// Resend cadence: keep sane and keepAlive strictly below the decay duration, else the panel blinks.
	if (g_MenusConfig.menu.htmlDurationSecs >= 1)
	{
		settings.htmlDurationSecs = g_MenusConfig.menu.htmlDurationSecs;
	}
	// Must stay below the decay duration, else the panel blinks between resends.
	if (g_MenusConfig.menu.htmlRefreshInterval > 0.0f && g_MenusConfig.menu.htmlRefreshInterval < settings.htmlDurationSecs)
	{
		settings.htmlRefreshInterval = g_MenusConfig.menu.htmlRefreshInterval;
	}
	if (g_MenusConfig.menu.htmlKeepAlive > 0.0f && g_MenusConfig.menu.htmlKeepAlive < settings.htmlDurationSecs)
	{
		settings.htmlKeepAlive = g_MenusConfig.menu.htmlKeepAlive;
	}
	if (html_style::IsSizeToken(g_MenusConfig.menu.htmlTitleSize))
	{
		settings.titleSize = g_MenusConfig.menu.htmlTitleSize;
	}
	if (html_style::IsSizeToken(g_MenusConfig.menu.htmlItemSize))
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
	g_Translations.SetResolveColorTags(false);
	g_Translations.Load(g_SMAPI->GetBaseDir(), "cs2menus");
	g_Translations.SetDefaultLanguage(g_MenusConfig.menu.defaultLanguage);
	g_MenuManager.SetLanguageResolver([](int slot) { return SlotLanguage(slot); });
}

// Server console / rcon command to reapply core.cfg without waiting for a map change.
CON_COMMAND_F(cs2menus_reload, "Reload cs2menus core.cfg and re-probe HTML availability.", FCVAR_RELEASE | FCVAR_GAMEDLL)
{
	int slot = context.GetPlayerSlot().Get();
	if (!MENU_AdminBridge_CanUseCommand(slot, "cs2menus_reload", CS2ADMIN_FLAG_ROOT))
	{
		MENU_PrintToChat(slot, "You don't have permission to use this command.");
		return;
	}
	LoadAndApplyConfig();
	EvaluateHtmlAvailability();
	MMU_LOG_INFO("Config reloaded.\n");
}

CON_COMMAND_F(cs2menus_version, "Print the cs2menus plugin and menu-API interface versions.", FCVAR_RELEASE | FCVAR_GAMEDLL)
{
	int slot = context.GetPlayerSlot().Get();
	if (!MENU_AdminBridge_CanUseCommand(slot, "cs2menus_version", CS2ADMIN_FLAG_ROOT))
	{
		MENU_PrintToChat(slot, "You don't have permission to use this command.");
		return;
	}
	MMU_LOG_INFO("Plugin version %s, interface %s.\n", PLUGIN_FULL_VERSION, CS2MENUS_INTERFACE);
}

// Per-player menu preferences (optional, sql_mm)
//
// The DB stores key choices by name (e.g. "shift"), so cs2menus keeps each slot's chosen names
// here as the source of truth for saving, and pushes the translated mask/label into the menu manager for rendering.
// The manager itself never deals in key names.

namespace
{
	// One slot's chosen preference names. Empty string = no preference (server default).
	struct SlotPrefs
	{
		uint64_t xuid = 0;
		std::string type; // "", "chat", "html"
		std::string up;   // key names, "" / "default" / "none" / "w" / ...
		std::string down;
		std::string select;
		std::string back;
	};

	SlotPrefs s_prefs[MAXPLAYERS + 1];
	// The slot's open preference menu, so commands can refresh its rows live. 0 = none.
	MenuHandle s_prefsMenu[MAXPLAYERS + 1] = {};

	bool ParseNavActionName(const char *name, MenuNavAction &out)
	{
		if (!name)
		{
			return false;
		}
		if (!strcmp(name, "up"))
		{
			out = MenuNavAction::Up;
			return true;
		}
		if (!strcmp(name, "down"))
		{
			out = MenuNavAction::Down;
			return true;
		}
		if (!strcmp(name, "select"))
		{
			out = MenuNavAction::Select;
			return true;
		}
		if (!strcmp(name, "back") || !strcmp(name, "exit"))
		{
			out = MenuNavAction::Back;
			return true;
		}
		return false;
	}

	// Human-readable label for a stored key name.
	std::string KeyDisplay(const std::string &name)
	{
		if (name.empty() || name == "default")
		{
			return "Default";
		}
		if (name == "none" || name == "off")
		{
			return "Disabled";
		}
		return NavKeyLabel(name); // uppercased key name, e.g. "SHIFT"
	}

	std::string TypeDisplay(const std::string &type)
	{
		if (type == "chat")
		{
			return "Chat";
		}
		if (type == "html")
		{
			return "HTML";
		}
		return "Server default";
	}

	// Push one action's stored name into the manager as a per-player nav preference.
	void ApplyOneNav(int slot, MenuNavAction action, const std::string &name)
	{
		if (name.empty() || name == "default")
		{
			g_MenuManager.ClearPlayerNavPref(slot, action);
			return;
		}
		if (name == "none" || name == "off")
		{
			g_MenuManager.SetPlayerNavDisabled(slot, action);
			return;
		}
		uint64_t mask = ParseNavKey(name);
		if (mask != 0)
		{
			g_MenuManager.SetPlayerNavPref(slot, action, mask, NavKeyLabel(name).c_str());
		}
		else
		{
			g_MenuManager.ClearPlayerNavPref(slot, action); // unknown name, fall back to server config
		}
	}

	// Push a slot's stored preference names into the menu manager.
	void ApplyPrefsToManager(int slot)
	{
		const SlotPrefs &p = s_prefs[slot];
		MenuType type = MenuType::Default;
		if (p.type == "chat")
		{
			type = MenuType::Chat;
		}
		else if (p.type == "html")
		{
			type = MenuType::Html;
		}
		g_MenuManager.SetPlayerTypePref(slot, type);
		ApplyOneNav(slot, MenuNavAction::Up, p.up);
		ApplyOneNav(slot, MenuNavAction::Down, p.down);
		ApplyOneNav(slot, MenuNavAction::Select, p.select);
		ApplyOneNav(slot, MenuNavAction::Back, p.back);
	}

	// Persist a slot's current preference names to the database.
	void SaveSlotPrefs(int slot)
	{
		if (!g_MenuPrefsDB.IsConnected() || s_prefs[slot].xuid == 0)
		{
			return;
		}
		MenuPrefsRow row;
		row.type = s_prefs[slot].type;
		row.keyUp = s_prefs[slot].up;
		row.keyDown = s_prefs[slot].down;
		row.keySelect = s_prefs[slot].select;
		row.keyBack = s_prefs[slot].back;
		g_MenuPrefsDB.SavePrefs(s_prefs[slot].xuid, row);
	}

	// Next value when cycling the type item: Default -> Chat -> (HTML) -> Default.
	std::string CycleType(const std::string &cur)
	{
		if (cur == "chat")
		{
			return g_MenuManager.HasHtml() ? "html" : "";
		}
		if (cur == "html")
		{
			return "";
		}
		return "chat"; // "" / "default" / unknown
	}

	// Next value when cycling a key item: default -> each key (keys::kKeys order) -> none -> default.
	// "default" clears (use server config), "none" disables the action.
	std::string CycleKey(const std::string &cur)
	{
		std::string c = cur.empty() ? "default" : cur;
		if (c == "default")
		{
			return keys::kKeys[0].canonical;
		}
		if (c == "none" || c == "off")
		{
			return "default";
		}
		const keys::KeyDef *k = keys::FindByName(c);
		if (!k)
		{
			return "default"; // unknown name
		}
		int idx = static_cast<int>(k - keys::kKeys);
		return (idx + 1 < keys::kKeyCount) ? keys::kKeys[idx + 1].canonical : "none";
	}

	// Refresh the slot's open preference menu rows (no-op if it isn't open).
	void RefreshPrefsItems(int slot)
	{
		MenuHandle menu = s_prefsMenu[slot];
		if (menu == kInvalidMenuHandle)
		{
			return;
		}
		const SlotPrefs &p = s_prefs[slot];
		char buf[128];
		snprintf(buf, sizeof(buf), "Menu style: %s", TypeDisplay(p.type).c_str());
		g_MenuManager.SetItemText(menu, 0, buf);
		snprintf(buf, sizeof(buf), "Up key: %s", KeyDisplay(p.up).c_str());
		g_MenuManager.SetItemText(menu, 1, buf);
		snprintf(buf, sizeof(buf), "Down key: %s", KeyDisplay(p.down).c_str());
		g_MenuManager.SetItemText(menu, 2, buf);
		snprintf(buf, sizeof(buf), "Select key: %s", KeyDisplay(p.select).c_str());
		g_MenuManager.SetItemText(menu, 3, buf);
		snprintf(buf, sizeof(buf), "Back key: %s", KeyDisplay(p.back).c_str());
		g_MenuManager.SetItemText(menu, 4, buf);
	}

	// Selection handler shared by every row of the preference menu.
	void OnPrefsSelect(MenuHandle /*menu*/, int slot, int item)
	{
		SlotPrefs &p = s_prefs[slot];
		switch (item)
		{
			case 0:
				p.type = CycleType(p.type);
				break;
			case 1:
				p.up = CycleKey(p.up);
				break;
			case 2:
				p.down = CycleKey(p.down);
				break;
			case 3:
				p.select = CycleKey(p.select);
				break;
			case 4:
				p.back = CycleKey(p.back);
				break;
			default:
				return;
		}
		ApplyPrefsToManager(slot);
		SaveSlotPrefs(slot);
		RefreshPrefsItems(slot); // live re-render with the new value
	}

	// True when the preference DB is connected.
	// Otherwise tells the player it's unavailable and returns false,
	// so callers can `if (!RequirePrefsDB(slot)) return;`.
	bool RequirePrefsDB(int slot)
	{
		if (g_MenuPrefsDB.IsConnected())
		{
			return true;
		}
		MENU_PrintToChat(slot, "Menu preferences are not available on this server.");
		return false;
	}

	void OpenPrefsMenu(int slot)
	{
		if (!RequirePrefsDB(slot))
		{
			return;
		}

		MenuHandle menu = g_MenuManager.CreateMenu(MenuType::Default, "Menu Preferences", OnPrefsSelect);
		if (menu == kInvalidMenuHandle)
		{
			return;
		}
		g_MenuManager.SetCloseOnSelect(menu, false); // stay open, cycle values in place
		g_MenuManager.AddItem(menu, "", "type", false);
		g_MenuManager.AddItem(menu, "", "up", false);
		g_MenuManager.AddItem(menu, "", "down", false);
		g_MenuManager.AddItem(menu, "", "select", false);
		g_MenuManager.AddItem(menu, "", "back", false);
		g_MenuManager.SetMenuEndCallback(menu,
										 [](MenuHandle endedMenu, int endSlot, MenuEndReason /*reason*/)
										 {
											 // Only forget the tracked handle if it's still this menu.
											 if (ValidSlot(endSlot) && s_prefsMenu[endSlot] == endedMenu)
											 {
												 s_prefsMenu[endSlot] = kInvalidMenuHandle;
											 }
											 g_MenuManager.DestroyMenu(endedMenu);
										 });
		s_prefsMenu[slot] = menu;
		RefreshPrefsItems(slot);
		// Opened from a command / say hook on the main thread, so curtime is available.
		CGlobalVars *globals = GetGameGlobals();
		g_MenuManager.DisplayMenu(menu, slot, 0.0f, globals ? globals->curtime : 0.0f);
	}

	// Reset a slot and load its stored preferences from the database (async).
	void LoadSlot(int slot, uint64_t xuid)
	{
		if (!ValidSlot(slot))
		{
			return;
		}
		s_prefs[slot] = SlotPrefs {};
		s_prefs[slot].xuid = xuid;
		g_MenuManager.ClearPlayerPrefs(slot);
		if (!g_MenuPrefsDB.IsConnected() || xuid == 0)
		{
			return;
		}
		g_MenuPrefsDB.LoadPrefs(xuid,
								[slot, xuid](bool found, const MenuPrefsRow &row)
								{
									// The slot may have a different occupant by the time this returns.
									if (s_prefs[slot].xuid != xuid || !found)
									{
										return;
									}
									s_prefs[slot].type = row.type;
									s_prefs[slot].up = row.keyUp;
									s_prefs[slot].down = row.keyDown;
									s_prefs[slot].select = row.keySelect;
									s_prefs[slot].back = row.keyBack;
									ApplyPrefsToManager(slot);
								});
	}
} // namespace

// Open the preference menu: "mm_menu_prefs" (client console) or the "!menu" chat alias.
CON_COMMAND_F(mm_menu_prefs, "Open your personal menu-preferences menu.", FCVAR_CLIENT_CAN_EXECUTE | FCVAR_GAMEDLL)
{
	int slot = context.GetPlayerSlot().Get();
	if (!ValidSlot(slot))
	{
		return;
	}
	if (!MENU_AdminBridge_CanUseCommand(slot, "menu_prefs", 0))
	{
		MENU_PrintToChat(slot, "You don't have permission to use this command.");
		return;
	}
	OpenPrefsMenu(slot);
}

CON_COMMAND_F(mm_pref_type, "Set your preferred menu style: chat | html | default.", FCVAR_CLIENT_CAN_EXECUTE | FCVAR_GAMEDLL)
{
	int slot = context.GetPlayerSlot().Get();
	if (!ValidSlot(slot) || args.ArgC() < 2)
	{
		return;
	}
	if (!MENU_AdminBridge_CanUseCommand(slot, "pref_type", 0))
	{
		MENU_PrintToChat(slot, "You don't have permission to use this command.");
		return;
	}
	if (!RequirePrefsDB(slot))
	{
		return;
	}
	std::string v = args.Arg(1);
	str::ToLowerInPlace(v);
	if (v == "chat" || v == "html")
	{
		s_prefs[slot].type = v;
	}
	else if (v == "default" || v == "server")
	{
		s_prefs[slot].type = "";
	}
	else
	{
		MENU_PrintToChat(slot, "Usage: mm_pref_type chat | html | default");
		return;
	}
	ApplyPrefsToManager(slot);
	SaveSlotPrefs(slot);
	RefreshPrefsItems(slot);
	MENU_PrintToChat(slot, "Menu style set to %s.", TypeDisplay(s_prefs[slot].type).c_str());
}

CON_COMMAND_F(mm_pref_key, "Set a menu navigation key: mm_pref_key <up|down|select|back> <key|default|none>.",
			  FCVAR_CLIENT_CAN_EXECUTE | FCVAR_GAMEDLL)
{
	int slot = context.GetPlayerSlot().Get();
	if (!ValidSlot(slot))
	{
		return;
	}
	if (!MENU_AdminBridge_CanUseCommand(slot, "pref_key", 0))
	{
		MENU_PrintToChat(slot, "You don't have permission to use this command.");
		return;
	}
	if (args.ArgC() < 3)
	{
		MENU_PrintToChat(slot, "Usage: mm_pref_key <up|down|select|back> <key|default|none>");
		return;
	}
	if (!RequirePrefsDB(slot))
	{
		return;
	}
	std::string actionName = args.Arg(1);
	std::string keyName = args.Arg(2);
	str::ToLowerInPlace(actionName);
	str::ToLowerInPlace(keyName);
	MenuNavAction action;
	if (!ParseNavActionName(actionName.c_str(), action))
	{
		MENU_PrintToChat(slot, "Unknown action. Use up, down, select or back.");
		return;
	}
	// Validate the key: a name ParseNavKey accepts, or the special "default"/"none".
	if (keyName != "default" && keyName != "none" && keyName != "off" && ParseNavKey(keyName) == 0)
	{
		MENU_PrintToChat(slot, "Unknown key \"%s\".", keyName.c_str());
		return;
	}
	switch (action)
	{
		case MenuNavAction::Up:
			s_prefs[slot].up = keyName;
			break;
		case MenuNavAction::Down:
			s_prefs[slot].down = keyName;
			break;
		case MenuNavAction::Select:
			s_prefs[slot].select = keyName;
			break;
		default:
			s_prefs[slot].back = keyName;
			break;
	}
	ApplyPrefsToManager(slot);
	SaveSlotPrefs(slot);
	RefreshPrefsItems(slot);
	MENU_PrintToChat(slot, "%s key set to %s.", actionName.c_str(), KeyDisplay(keyName).c_str());
}

CON_COMMAND_F(mm_pref_show, "Show your current menu preferences.", FCVAR_CLIENT_CAN_EXECUTE | FCVAR_GAMEDLL)
{
	int slot = context.GetPlayerSlot().Get();
	if (!ValidSlot(slot))
	{
		return;
	}
	if (!MENU_AdminBridge_CanUseCommand(slot, "pref_show", 0))
	{
		MENU_PrintToChat(slot, "You don't have permission to use this command.");
		return;
	}
	if (!RequirePrefsDB(slot))
	{
		return;
	}
	const SlotPrefs &p = s_prefs[slot];
	MENU_PrintToChat(slot, "Style: %s | Up: %s | Down: %s | Select: %s | Back: %s", TypeDisplay(p.type).c_str(), KeyDisplay(p.up).c_str(),
					 KeyDisplay(p.down).c_str(), KeyDisplay(p.select).c_str(), KeyDisplay(p.back).c_str());
}

bool CS2MenusPlugin::Load(PluginId id, ISmmAPI *ismm, char *error, size_t maxlen, bool late)
{
	PLUGIN_SAVEVARS();

	mmu::log::Setup logSetup;
	logSetup.channelName = "CS2Menus";
	logSetup.addonName = "cs2menus";
	logSetup.toFile = true;
	mmu::log::Init(logSetup);

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
	SH_ADD_HOOK(IServerGameClients, OnClientConnected, g_pGameClients, SH_MEMBER(this, &CS2MenusPlugin::Hook_OnClientConnected), false);
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

	MMU_LOG_INFO("Plugin loaded.\n");
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

void CS2MenusPlugin::AllPluginsLoaded()
{
	// Resolve mm-cs2admin so admin_overrides.cfg can gate our commands.
	MENU_AdminBridge_Init();

	// Per-player preferences are opt-in and need sql_mm, which must be fully loaded by now.
	if (!g_MenusConfig.database.enabled)
	{
		return;
	}
	if (!g_MenuPrefsDB.Init())
	{
		MMU_LOG_WARN("Preference database unavailable, per-player preferences disabled.\n");
		return;
	}
	g_MenuPrefsDB.Connect(
		[](bool success)
		{
			if (!success)
			{
				return;
			}
			// Late load: pick up players already connected.
			for (int slot = 0; slot <= MAXPLAYERS; slot++)
			{
				uint64 xuid = g_pEngine->GetClientXUID(CPlayerSlot(slot));
				if (xuid != 0)
				{
					LoadSlot(slot, xuid);
				}
			}
		});
}

void CS2MenusPlugin::OnPluginLoad(PluginId /*id*/)
{
	MENU_AdminBridge_Refresh();
}

void CS2MenusPlugin::OnPluginUnload(PluginId /*id*/)
{
	MENU_AdminBridge_Refresh();
}

bool CS2MenusPlugin::Unload(char *error, size_t maxlen)
{
	MENU_AdminBridge_Shutdown();

	SH_REMOVE_HOOK(IServerGameDLL, GameFrame, g_pServerGameDLL, SH_MEMBER(this, &CS2MenusPlugin::Hook_GameFrame), true);
	SH_REMOVE_HOOK(IServerGameClients, OnClientConnected, g_pGameClients, SH_MEMBER(this, &CS2MenusPlugin::Hook_OnClientConnected), false);
	SH_REMOVE_HOOK(IServerGameClients, ClientDisconnect, g_pGameClients, SH_MEMBER(this, &CS2MenusPlugin::Hook_ClientDisconnect), true);
	SH_REMOVE_HOOK(ICvar, DispatchConCommand, g_pICvar, SH_MEMBER(this, &CS2MenusPlugin::Hook_DispatchConCommand), false);

	// Drop all menus + displays without firing callbacks into consumer plugins.
	g_MenuManager.Shutdown();
	g_MenuPrefsDB.Shutdown();
	center_html::Shutdown();

	mmu::log::Shutdown();

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

void CS2MenusPlugin::Hook_OnClientConnected(CPlayerSlot slot, const char * /*pszName*/, uint64 xuid, const char * /*pszNetworkID*/,
											const char * /*pszAddress*/, bool bFakePlayer)
{
	int s = slot.Get();
	if (!bFakePlayer && ValidSlot(s))
	{
		LoadSlot(s, xuid);
	}
	RETURN_META(MRES_IGNORED);
}

void CS2MenusPlugin::Hook_ClientDisconnect(CPlayerSlot slot, ENetworkDisconnectionReason /*reason*/, const char * /*pszName*/, uint64 /*xuid*/,
										   const char * /*pszNetworkID*/)
{
	int s = slot.Get();
	g_MenuManager.OnPlayerDisconnect(s);
	if (ValidSlot(s))
	{
		g_MenuManager.ClearPlayerPrefs(s);
		s_prefs[s] = SlotPrefs {};
		s_prefsMenu[s] = kInvalidMenuHandle;
	}
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

// Strip CS2's outer quotes and surrounding whitespace from a say message,
// then the configured chat or silent prefix.
// On a prefix match, `body` is the text after the prefix
// and `silent` is set when the silent prefix matched. Returns false otherwise.
static bool StripCommandPrefix(const char *rawMsg, std::string &body, bool &silent)
{
	silent = false;
	body.clear();
	std::string msg = rawMsg ? rawMsg : "";
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
	if (!p1.empty() && msg.rfind(p1, 0) == 0)
	{
		body = msg.substr(p1.size());
		return true;
	}
	if (!p2.empty() && msg.rfind(p2, 0) == 0)
	{
		body = msg.substr(p2.size());
		silent = true; // only the silent prefix hides the chat line
		return true;
	}
	return false;
}

// Parse a say message like "!menu_up", "/menu_close" or "!menu_select 3" into a nav action
// plus an optional number (-1 if none, used by menu_select on chat menus).
// `silent` is set when the silent prefix matched (only then is the chat line hidden).
// Requires the configured chat or silent prefix, returns false otherwise.
static bool ParseChatMenuNav(const char *rawMsg, MenuNavAction &action, int &number, bool &silent)
{
	number = -1;
	std::string body;
	if (!StripCommandPrefix(rawMsg, body, silent))
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
	str::ToLowerInPlace(cmd);
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
// commandName gates the nav command via admin_overrides.cfg.
static void RunMenuNavCommand(const CCommandContext &context, const char *commandName, MenuNavAction action)
{
	int slot = context.GetPlayerSlot().Get();
	if (!ValidSlot(slot))
	{
		return;
	}
	if (!MENU_AdminBridge_CanUseCommand(slot, commandName, 0))
	{
		return;
	}
	CGlobalVars *globals = GetGameGlobals();
	g_MenuManager.CommandNav(slot, action, globals ? globals->curtime : 0.0f);
}

CON_COMMAND_F(mm_menu_up, "Move the open menu's cursor up.", FCVAR_CLIENT_CAN_EXECUTE | FCVAR_GAMEDLL)
{
	RunMenuNavCommand(context, "menu_up", MenuNavAction::Up);
}

CON_COMMAND_F(mm_menu_down, "Move the open menu's cursor down.", FCVAR_CLIENT_CAN_EXECUTE | FCVAR_GAMEDLL)
{
	RunMenuNavCommand(context, "menu_down", MenuNavAction::Down);
}

// "mm_menu_select" selects the highlighted HTML row, or "mm_menu_select 3" picks chat entry 3.
// Server console returns.
CON_COMMAND_F(mm_menu_select, "Select a menu item: no arg = highlighted row, a number = that chat entry.", FCVAR_CLIENT_CAN_EXECUTE | FCVAR_GAMEDLL)
{
	int slot = context.GetPlayerSlot().Get();
	if (!ValidSlot(slot))
	{
		return;
	}
	if (!MENU_AdminBridge_CanUseCommand(slot, "menu_select", 0))
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
	RunMenuNavCommand(context, "menu_close", MenuNavAction::Back);
}

// Strip the outer quotes / surrounding whitespace / configured prefix from a say message
// and return the lowercased first word. Returns false if no prefix matched.
// `silent` is set when the silent prefix matched.
static bool ParseChatPrefixWord(const char *rawMsg, std::string &word, bool &silent)
{
	word.clear();
	std::string body;
	if (!StripCommandPrefix(rawMsg, body, silent))
	{
		return false;
	}
	size_t sp = body.find_first_of(" \t");
	word = (sp == std::string::npos) ? body : body.substr(0, sp);
	str::ToLowerInPlace(word);
	return true;
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
	if (!ValidSlot(slot))
	{
		RETURN_META(MRES_IGNORED);
	}

	// "!menu" / "!prefs" opens the preference menu, with or without a menu already open.
	// Only active when the preference database is connected, so with the feature off cs2menus
	// never touches these say words (a consumer plugin may use them for its own purpose).
	if (g_MenuPrefsDB.IsConnected())
	{
		std::string word;
		bool silent;
		if (ParseChatPrefixWord(args.ArgS(), word, silent) && (word == "menu" || word == "prefs" || word == "menuprefs"))
		{
			if (MENU_AdminBridge_CanUseCommand(slot, "menu_prefs", 0))
			{
				OpenPrefsMenu(slot);
			}
			RETURN_META(silent ? MRES_SUPERCEDE : MRES_IGNORED);
		}
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
		// Match the override key to the equivalent console nav command.
		const char *navCmd = "menu_close";
		switch (navAction)
		{
			case MenuNavAction::Up:
				navCmd = "menu_up";
				break;
			case MenuNavAction::Down:
				navCmd = "menu_down";
				break;
			case MenuNavAction::Select:
				navCmd = "menu_select";
				break;
			default:
				navCmd = "menu_close";
				break;
		}
		if (!MENU_AdminBridge_CanUseCommand(slot, navCmd, 0))
		{
			RETURN_META(navSilent ? MRES_SUPERCEDE : MRES_IGNORED);
		}
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

	// Typing a bare item number in chat selects that row, same as menu_select.
	if (MENU_AdminBridge_CanUseCommand(slot, "menu_select", 0) && g_MenuManager.ProcessInput(slot, rawMsg, curtime))
	{
		// Suppress the chat line so the number doesn't show.
		RETURN_META(MRES_SUPERCEDE);
	}

	RETURN_META(MRES_IGNORED);
}
