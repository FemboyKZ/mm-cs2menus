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
static constexpr int MENU_MAX_HTML_VISIBLE = 7;

// Settings parsed from core.cfg and pushed in via Configure.
struct MenuManagerSettings
{
	// Chat: leading chars allowed before a selection number.
	std::string acceptedPrefixes = "!/";
	// Chat: items per page (clamped 1..MENU_MAX_ITEMS_PER_PAGE).
	int itemsPerPage = MENU_MAX_ITEMS_PER_PAGE;
	// Resolves MenuType::Default at CreateMenu time.
	MenuType defaultType = MenuType::Chat;
	// Exit-button default for new menus.
	bool defaultExitButton = true;
	// HTML: default for the inline selectable "Exit" row on new menus.
	bool defaultExitItem = false;

	// HTML: rows visible at once (clamped 1..MENU_MAX_HTML_VISIBLE).
	int htmlVisibleItems = MENU_MAX_HTML_VISIBLE;
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
};

// Backing store for the public ICS2Menus API. Holds menus by handle plus
// per-player display state, renders chat + HTML menus, and routes input.
class MenuManager
{
public:
	// --- API ---
	MenuHandle CreateMenu(MenuType type, const char *title, MenuItemSelectFn onSelect);
	int AddItem(MenuHandle menu, const char *text, const char *info, bool disabled);
	void SetTitle(MenuHandle menu, const char *title);
	void SetExitButton(MenuHandle menu, bool enabled);
	void SetCloseOnSelect(MenuHandle menu, bool enabled);
	void SetMenuEndCallback(MenuHandle menu, MenuEndFn onEnd);
	void SetMenuKey(MenuHandle menu, MenuNavAction action, MenuButton button);
	void SetExitItem(MenuHandle menu, bool enabled);

	bool DisplayMenu(MenuHandle menu, int slot, float duration, float curtime);
	void CancelMenu(int slot);
	bool HasMenu(int slot) const;
	MenuHandle GetActiveMenu(int slot) const;
	MenuType GetActiveMenuType(int slot) const;
	void DestroyMenu(MenuHandle menu);

	int GetItemCount(MenuHandle menu) const;
	const char *GetItemText(MenuHandle menu, int item) const;
	const char *GetItemInfo(MenuHandle menu, int item) const;

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

	// Expire timed-out menus and re-send HTML. Call every GameFrame.
	void Tick(float curtime);

	// Close a leaving player's menu (fires MenuEnd=Disconnect).
	void OnPlayerDisconnect(int slot);

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

private:
	struct MenuItem
	{
		std::string text;
		std::string info;
		bool disabled = false;
	};

	// Per-menu HTML nav-key overrides, indexed by MenuNavAction (Up/Down/Select/Back).
	// mask 0 = inherit the server config binding for that action.
	struct NavOverride
	{
		uint64_t mask = 0;
		std::string label;
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
		NavOverride navOverride[4];
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

	void Render(int slot);     // dispatch to RenderPage/RenderHtml by the slot's menu type
	void RenderPage(int slot); // chat
	void RenderHtml(int slot); // html

	// html navigation
	void HtmlMoveCursor(int slot, int delta);

	// Effective nav binding for an action (per-menu override, else server config).
	uint64_t EffectiveNavMask(const MenuDef &def, int action) const;
	std::string EffectiveNavLabel(const MenuDef &def, int action) const;

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
};

extern MenuManager g_MenuManager;

#endif // _INCLUDE_MENU_MANAGER_H_
