#ifndef _INCLUDE_MENU_MANAGER_H_
#define _INCLUDE_MENU_MANAGER_H_

#include "src/common.h"
#include "src/public/ics2menus.h"

#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

// Hard ceiling on chat-menu items per page: keys 1-7 select, 8/9 page, 0 exits.
static constexpr int MENU_MAX_ITEMS_PER_PAGE = 7;
// HTML menus scroll with movement keys, so they aren't bound by the 1-7 chat key limit.
// This is just a sanity ceiling on the on-screen window.
static constexpr int MENU_MAX_HTML_VISIBLE = 20;

// Settings parsed from core.cfg and pushed in via Configure.
struct MenuManagerSettings
{
	// Chat: leading chars allowed before a selection number.
	std::string acceptedPrefixes = "!/";
	// Chat: items per page (clamped 1..MENU_MAX_ITEMS_PER_PAGE).
	int itemsPerPage = MENU_MAX_ITEMS_PER_PAGE;
	// Chat styling. Colors are resolved control-byte strings (CHAT_COLOR_*), decoration is literal text.
	std::string chatTitleColor = CHAT_COLOR_ORCHID;
	std::string chatPageColor = CHAT_COLOR_DEFAULT;
	std::string chatItemColor = CHAT_COLOR_DEFAULT;
	std::string chatDisabledColor = CHAT_COLOR_GREY;
	std::string chatArrowColor = CHAT_COLOR_ORCHID;
	std::string chatHeaderColor = CHAT_COLOR_DEFAULT;
	std::string chatTitlePrefix = "-- ";
	std::string chatTitleSuffix = " --";
	std::string chatNumberPrefix = "#";
	std::string chatNumberSuffix = " ";
	std::string chatDisabledPrefix = "#";
	std::string chatArrow = "-> ";
	std::string chatPagePrefix = "(page ";
	std::string chatPageSuffix = ")";
	bool chatShowPage = true;
	std::string chatHeader;
	// Resolves MenuType::Default at CreateMenu time.
	MenuType defaultType = MenuType::Chat;
	// Exit-button default for new menus.
	bool defaultExitButton = true;
	// HTML: default for the inline selectable "Exit" row on new menus.
	bool defaultExitItem = false;

	// HTML: rows visible at once (clamped 1..MENU_MAX_HTML_VISIBLE).
	int htmlVisibleItems = 7;
	// HTML: button bitmasks (IN_*) for navigation. Defaults = WASD.
	uint64_t keyUp = 0x8;       // W (IN_FORWARD)
	uint64_t keyDown = 0x10;    // S (IN_BACK)
	uint64_t keySelect = 0x400; // D (IN_MOVERIGHT)
	uint64_t keyBack = 0x200;   // A (IN_MOVELEFT)
	// HTML: display labels for the footer key hints (uppercased key names).
	std::string keyUpLabel = "W";
	std::string keyDownLabel = "S";
	std::string keySelectLabel = "D";
	std::string keyBackLabel = "A";
	// HTML: hex colors for markup.
	std::string navColor = "#ff2ee7";
	std::string footerColor = "#909090";
	std::string disabledColor = "#808080";
	// HTML: title accent color, size tokens (s/sm/m/ml/l), and whether lines are centered.
	std::string titleColor = "#ff00e1";
	std::string titleSize = "l";
	std::string itemSize = "sm";
	bool centered = true;
	// HTML: normal item text color, cursor marker text, and an optional Panorama font
	// class applied to every line (empty = the game's default font).
	std::string itemColor = "#FFFFFF";
	std::string marker = "\xE2\x96\xB6 "; // ▶
	std::string fontFace;
	// HTML: position counter + key-hint footer styling and visibility.
	std::string counterColor = "#9aa0a6";
	std::string footerSize = "s";
	bool showCounter = true;
	bool showFooter = true;
	// HTML: submenu-item suffix, footer segment separator, counter brackets, and whether
	// the cursor row's text is recolored (vs. marked only by the marker).
	std::string submenuSuffix = " >"; // »
	std::string footerSeparator = " | ";
	std::string counterPrefix = "[";
	std::string counterSuffix = "]";
	bool highlightText = true;
	// HTML: center-panel resend cadence (the message decays, so it's re-sent while open).
	// keepAlive must stay below durationSecs or the panel can blink.
	int htmlDurationSecs = 3;
	float htmlRefreshInterval = 1.0f;
	float htmlKeepAlive = 2.0f;
};

// Backing store for the public ICS2Menus API. Holds menus by handle plus
// per-player display state, renders chat + HTML menus, and routes input.
class MenuManager
{
public:
	// --- API ---
	MenuHandle CreateMenu(MenuType type, const char *title, MenuItemSelectFn onSelect);
	int AddItem(MenuHandle menu, const char *text, const char *info, bool disabled);
	int AddSubMenu(MenuHandle parent, const char *text, MenuHandle child, const char *info);
	void SetTitle(MenuHandle menu, const char *title);
	void SetExitButton(MenuHandle menu, bool enabled);
	void SetCloseOnSelect(MenuHandle menu, bool enabled);
	void SetMenuEndCallback(MenuHandle menu, MenuEndFn onEnd);
	void SetMenuKey(MenuHandle menu, MenuNavAction action, MenuButton button);
	void SetExitItem(MenuHandle menu, bool enabled);
	void SetMenuLabel(MenuHandle menu, MenuLabel label, const char *text);
	const char *GetMenuLabel(MenuHandle menu, MenuLabel label) const;
	void SetMenuStyle(MenuHandle menu, MenuStyle field, const char *value);
	const char *GetMenuStyle(MenuHandle menu, MenuStyle field) const;

	// Live item mutation. Any player currently viewing the menu is re-rendered.
	void SetItemText(MenuHandle menu, int item, const char *text);
	void SetItemInfo(MenuHandle menu, int item, const char *info);
	void SetItemDisabled(MenuHandle menu, int item, bool disabled);
	void RemoveItem(MenuHandle menu, int item);
	void RemoveAllItems(MenuHandle menu);

	bool GetItemDisabled(MenuHandle menu, int item) const;
	void SetItemRaw(MenuHandle menu, int item, bool raw);
	bool GetItemRaw(MenuHandle menu, int item) const;
	void SetItemIcon(MenuHandle menu, int item, const char *url);
	const char *GetItemIcon(MenuHandle menu, int item) const;

	// Item the menu opens on (HTML cursor / chat page). Clamped at display.
	void SetStartItem(MenuHandle menu, int item);
	int GetStartItem(MenuHandle menu) const;

	bool DisplayMenu(MenuHandle menu, int slot, float duration, float curtime);
	void CancelMenu(int slot);
	bool HasMenu(int slot) const;
	MenuHandle GetActiveMenu(int slot) const;
	MenuType GetActiveMenuType(int slot) const;
	void DestroyMenu(MenuHandle menu);

	// Abs index of the highlighted item for an HTML menu, or -1 if none / on the Exit row / chat.
	int GetSelectedItem(int slot) const;

	int GetItemCount(MenuHandle menu) const;
	const char *GetItemText(MenuHandle menu, int item) const;
	const char *GetItemInfo(MenuHandle menu, int item) const;

	// Run fn on the main thread: inline if already there, else queued for the next GameFrame.
	// Used by DisplayMenuToAll, which needs main-thread entity access to enumerate players.
	void RunOnMainThread(std::function<void()> fn);

	// Feed a player's raw say message. Returns true if it was consumed as a
	// chat-menu action (caller should suppress the chat line).
	bool ProcessInput(int slot, const char *text, float curtime);

	// True if `slot` has an active HTML menu and thus needs per-frame button polling.
	bool WantsButtonInput(int slot) const;

	// True if any player currently has an HTML menu open.
	bool AnyHtmlMenuActive() const;

	// Feed a player's current held-button bitmask (read from the pawn each frame).
	// Drives HTML-menu navigation on newly-pressed nav keys.
	void PollButtons(int slot, uint64_t heldButtons, float curtime);

	// Drive the open menu from a console/chat command (backup for button input).
	// No-op without an open menu.
	// Up/Down/Select apply to HTML menus only, Back closes either menu type.
	void CommandNav(int slot, MenuNavAction action, float curtime);

	// Select by number (backup for chat menus, e.g. bind a key to "mm_menu_select 3").
	// Routes the number through chat-menu paging/selection (clamped to the page).
	// HTML menus ignore the number and select the cursor row.
	void CommandSelectNumber(int slot, int number, float curtime);

	// Expire timed-out menus and re-send HTML. Call every GameFrame.
	void Tick(float curtime);

	// Close a leaving player's menu (fires MenuEnd=Disconnect).
	void OnPlayerDisconnect(int slot);

	// Yield this slot to a host menu system (SwiftlyS2 / CS#).
	// When busy is set, any open cs2menus menu for the slot is cancelled
	// and further DisplayMenu calls for it are refused until cleared.
	// The host drives this off its own menu open/close. cs2menus never auto-reopens on clear.
	void SetExternalBusy(int slot, bool busy);
	bool GetExternalBusy(int slot) const;

	// Drop everything without firing callbacks. Call from plugin Unload().
	void Shutdown();

	// Apply settings parsed from core.cfg.
	void Configure(const MenuManagerSettings &settings);

	// Record the calling thread as the main/game thread. Call once from Load.
	// Lets the API run engine-touching work inline, or defer it to GameFrame off-thread.
	void SetMainThread();

	bool OnMainThread() const;

	// Whether HTML menus can render + receive input (set by the plugin after probing).
	// When false, CreateMenu downgrades any HTML menu to chat so it stays usable.
	void SetHtmlAvailable(bool available);

	// Resolve a viewing player's language key for label translation.
	// Set by the plugin from the optional ClientCvarValue interface.
	// When unset (or it returns ""), the translation default language is used.
	void SetLanguageResolver(std::function<std::string(int slot)> resolver);

private:
	struct MenuItem
	{
		std::string text;
		std::string info;
		bool disabled = false;
		// HTML: text is raw Panorama markup, rendered unescaped (see SetItemRaw).
		bool raw = false;
		// HTML: optional image drawn before the text (see SetItemIcon).
		std::string iconUrl;
		// Selecting this item navigates into another menu instead of firing onSelect.
		MenuHandle submenu = kInvalidMenuHandle;
	};

	// Per-menu HTML nav-key overrides, indexed by MenuNavAction (Up/Down/Select/Back).
	// mask 0 = inherit the server config binding for that action.
	struct NavOverride
	{
		uint64_t mask = 0;
		std::string label;
	};

	// Per-menu HTML style overrides. Empty string / centered<0 means inherit the server default.
	struct StyleOverride
	{
		std::string titleColor;
		std::string titleSize; // size token (s/sm/m/ml/l)
		std::string itemSize;
		std::string navColor;
		std::string footerColor;
		std::string disabledColor;
		std::string itemColor;
		std::string fontFace;
		std::string marker; // empty = inherit (use a space for "no marker")
		std::string counterColor;
		std::string footerSize;
		std::string submenuSuffix; // empty = inherit (a space = no suffix)
		std::string footerSeparator;
		std::string counterPrefix;
		std::string counterSuffix;
		int centered = -1;      // -1 inherit, 0 off, 1 on
		int showCounter = -1;   // -1 inherit, 0 off, 1 on
		int showFooter = -1;    // -1 inherit, 0 off, 1 on
		int highlightText = -1; // -1 inherit, 0 off, 1 on
	};

	struct MenuDef
	{
		MenuType type = MenuType::Chat;
		std::string title;
		std::vector<MenuItem> items;
		MenuItemSelectFn onSelect;
		MenuEndFn onEnd;
		bool exitButton = true;
		bool closeOnSelect = true;
		bool exitItem = false; // HTML: show a selectable "Exit" row in the list
		int startItem = 0;     // item the menu opens on
		// Set when this menu is reached as a submenu, so Back returns to the parent.
		MenuHandle parent = kInvalidMenuHandle;
		NavOverride navOverride[4];
		// Built-in labels, seeded from settings at CreateMenu, indexed by MenuLabel.
		std::string labels[static_cast<int>(MenuLabel::Count)];
		StyleOverride style;
	};

	struct PlayerMenu
	{
		bool active = false;
		MenuHandle handle = kInvalidMenuHandle;
		int page = 0;            // chat pagination
		int cursor = 0;          // html selected option (abs index)
		float expireTime = 0.0f; // absolute game time, 0 = no expire
		uint64_t prevButtons = 0;
		bool buttonsPrimed = false;
		float nextHtmlRender = 0.0f;
		// Last HTML actually sent + when, so identical refreshes can be skipped.
		std::string lastHtml;
		float lastHtmlSend = 0.0f;
		// A host UI (SwiftlyS2 / CS# menu) owns this slot's screen.
		// While set, we refuse to display so we never fight the host for input or the HTML channel.
		bool externalBusy = false;
	};

	MenuDef *Find(MenuHandle menu);
	const MenuDef *Find(MenuHandle menu) const;

	// Core of DisplayMenu.
	// Assumes m_mutex held, runs on main, schedules off m_curtime.
	bool DisplayLocked(MenuHandle menu, int slot, float duration);

	// Close slot's display and fire its MenuEnd.
	// Safe against the callback destroying the menu.
	void EndDisplay(int slot, MenuEndReason reason);

	// Run a selection: invoke onSelect, then close + fire End(Selected)
	// if closeOnSelect, else re-render. Shared by chat and html.
	void Select(int slot, int itemIndex);

	// Swap the slot's displayed menu to `handle` without firing end callbacks.
	// Used for submenu navigation (into a child, or Back to a parent).
	void SwitchMenu(int slot, MenuHandle handle);

	void Render(int slot);     // dispatch to RenderPage/RenderHtml by the slot's menu type
	void RenderPage(int slot); // chat
	void RenderHtml(int slot); // html

	// Re-render every player currently viewing `menu` (after a live mutation).
	// Defers to the next GameFrame if called off the main thread.
	void RefreshMenu(MenuHandle menu);

	// html navigation
	void HtmlMoveCursor(int slot, int delta);
	// Activate the cursor row (exit row closes, else selects the item). HTML only.
	void HtmlNavSelect(int slot);
	// Step back to the parent submenu, or exit the menu. Chat or HTML.
	void NavClose(int slot);
	// Apply a chat-menu number (1..page select, Next/Prev/Exit reserved). True if consumed.
	bool ApplyChatNumber(int slot, int num);

	// Effective nav binding for an action (per-menu override, else server config).
	uint64_t EffectiveNavMask(const MenuDef &def, MenuNavAction action) const;
	std::string EffectiveNavLabel(const MenuDef &def, MenuNavAction action) const;

	// Built-in phrase key for a label (seeds MenuDef, restores on SetMenuLabel("")).
	static const char *DefaultLabelKey(MenuLabel label);
	// Translate this menu's label for the player viewing in `slot`.
	// Resolves the viewer's language, then looks the per-menu key up in the phrase table.
	std::string ResolveLabel(int slot, const MenuDef &def, MenuLabel label) const;

	// HTML: whether to render the selectable "Exit" row (after the last item).
	// Shown when the menu is exitable and either the toggle is on or the Back key
	// is disabled, so a menu is never left unexitable.
	bool HtmlShowsExitRow(const MenuDef &def) const;
	// HTML: total navigable rows = items + (exit row ? 1 : 0).
	int HtmlRowCount(const MenuDef &def) const;

	std::unordered_map<MenuHandle, MenuDef> m_menus;
	MenuHandle m_nextHandle = 1;
	PlayerMenu m_players[MAXPLAYERS + 1];

	// Guards all of the above. Recursive: a callback fired while held may re-enter the API.
	mutable std::recursive_mutex m_mutex;

	// The game thread (render/callbacks must run here). Set by SetMainThread in Load.
	std::thread::id m_mainThread;

	// Queued by off-thread callers, drained on main at the top of Tick (lock held).
	std::vector<std::function<void()>> m_pending;

	MenuManagerSettings m_settings;
	// Effective clamped copies of the size settings.
	int m_itemsPerPage = MENU_MAX_ITEMS_PER_PAGE;
	int m_htmlVisibleItems = MENU_MAX_HTML_VISIBLE;
	// HTML rendering+input usable (see SetHtmlAvailable). Off until proven.
	bool m_htmlAvailable = false;
	// Guards against unbounded reentrancy when a consumer callback re-enters API.
	int m_callbackDepth = 0;
	// Latest game time, refreshed by Tick/PollButtons/DisplayMenu so HTML render
	// scheduling doesn't need curtime threaded through every call.
	float m_curtime = 0.0f;

	// Maps a slot to its language key for label translation (see SetLanguageResolver).
	std::function<std::string(int)> m_langResolver;
};

extern MenuManager g_MenuManager;

#endif // _INCLUDE_MENU_MANAGER_H_
