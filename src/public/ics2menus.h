#ifndef _INCLUDE_ICS2MENUS_H_
#define _INCLUDE_ICS2MENUS_H_

#include <cstdint>
#include <functional>

// Public menu API for CS2Menus.
//
// Other Metamod plugins acquire this interface via:
//   ICS2Menus *menus = (ICS2Menus *)g_SMAPI->MetaFactory(
//       CS2MENUS_INTERFACE, nullptr, nullptr);
//
// cs2menus owns player chat input: while a player has an open menu, the plugin
// intercepts their "say"/"say_team" numeric input, drives the menu, and suppresses the chat line.
// Consumers only build menus and react to selections.
//
// ABI note: this interface is consumed by sibling plugins built with the SAME toolchain as cs2menus,
// so passing std::function / std::string across the boundary is safe here
#define CS2MENUS_INTERFACE "ICS2Menus001"

// Opaque menu identifier returned by CreateMenu. 0 is the invalid sentinel.
// A handle stays valid until DestroyMenu (or until cs2menus unloads).
using MenuHandle = uint32_t;
static constexpr MenuHandle kInvalidMenuHandle = 0;

// Render style for a menu.
enum class MenuType : int
{
	Default = -1, // use the server's configured default type (see core.cfg)
	Chat = 0,     // numbered list printed to chat, navigated by typing 1-9/0
	Html = 1,     // center-screen HTML panel, navigated with movement keys
};

// Why a player's menu closed. Delivered to the MenuEnd callback.
enum class MenuEndReason : int
{
	Selected = 0,   // player picked a (non-disabled) item, OnSelect already fired
	Exit = 1,       // player pressed the Exit button (slot 0)
	Timeout = 2,    // display duration elapsed
	Disconnect = 3, // player left the server
	Cancelled = 4,  // CancelMenu, or replaced by a newer DisplayMenu
	Destroyed = 5,  // the menu handle was destroyed while displayed
};

// Buttons usable as HTML-menu navigation keys (for per-menu key overrides).
// These map to the player's in-game button binds.
enum class MenuButton : int
{
	Default = 0, // inherit the server config binding for this action
	W,
	A,
	S,
	D,
	Use,     // E
	Speed,   // Shift (walk)
	Duck,    // Ctrl
	Jump,    // Space
	Reload,  // R
	Attack,  // Mouse1
	Attack2, // Mouse2
	Score,   // Tab
	None,    // disable this action for the menu
};

// HTML-menu navigation actions whose key can be overridden per menu.
enum class MenuNavAction : int
{
	Up = 0, // move cursor up
	Down,   // move cursor down
	Select, // activate highlighted item
	Back,   // close / exit
};

// Fired when a player selects an item.
// `item` is the absolute index into the menu (0-based, across pages), not the on-screen 1-9 slot.
// Use GetItemInfo / GetItemText to recover what was chosen.
using MenuItemSelectFn = std::function<void(MenuHandle menu, int slot, int item)>;

// Fired exactly once when a player's display of `menu` ends, for any reason.
// For Selected, this fires after the MenuItemSelectFn.
// Use it to free per-menu state (e.g. call DestroyMenu for one-shot menus).
using MenuEndFn = std::function<void(MenuHandle menu, int slot, MenuEndReason reason)>;

class ICS2Menus
{
public:
	// --- Build ---

	// Create an empty menu of `type` (pass MenuType::Default to use the server's configured style).
	// `title` may contain chat color codes.
	// `onSelect` is invoked when a player picks an item (may be null if you only care about MenuEnd).
	// Returns kInvalidMenuHandle on failure.
	virtual MenuHandle CreateMenu(MenuType type, const char *title, MenuItemSelectFn onSelect) = 0;

	// Append an item.
	// `info` is an opaque tag echoed back via GetItemInfo, pass "" if unused.
	// A `disabled` item is shown greyed out and cannot be selected.
	// Returns the new item's absolute index, or -1 on failure.
	virtual int AddItem(MenuHandle menu, const char *text, const char *info, bool disabled) = 0;

	virtual void SetTitle(MenuHandle menu, const char *title) = 0;

	// Show/hide the trailing "0. Exit" entry (default: shown).
	virtual void SetExitButton(MenuHandle menu, bool enabled) = 0;

	// Close the menu automatically after a selection (default: true).
	// When false, the menu is re-rendered after each pick so the player can choose again.
	virtual void SetCloseOnSelect(MenuHandle menu, bool enabled) = 0;

	// Register the per-menu end callback. See MenuEndFn.
	virtual void SetMenuEndCallback(MenuHandle menu, MenuEndFn onEnd) = 0;

	// HTML menus: show a selectable "Exit" row at the end of the list (default off).
	// Useful when the Back key is set to None, it's also auto-shown in that case so a
	// menu is never left unexitable. Requires SetExitButton(true). No-op for chat menus.
	virtual void SetExitItem(MenuHandle menu, bool enabled) = 0;

	// Override the navigation key for one action on an HTML menu.
	// Pass MenuButton::Default to clear the override and fall back to the server's configured binding.
	// Pass MenuButton::None to disable the action for this menu
	// (e.g. disable Up so a single key cycles through items, the cursor wraps).
	// The footer key hints update to match.
	virtual void SetMenuKey(MenuHandle menu, MenuNavAction action, MenuButton button) = 0;

	// --- Show / hide ---

	// Display `menu` to `slot` for `duration` seconds (0 = no timeout).
	// Replaces any menu the player currently has open (firing its MenuEnd=Cancelled first).
	// Returns false for an invalid handle/slot.
	virtual bool DisplayMenu(MenuHandle menu, int slot, float duration) = 0;

	// Close whatever menu `slot` has open (fires MenuEnd=Cancelled). No-op if none.
	virtual void CancelMenu(int slot) = 0;

	// True if `slot` currently has any menu open.
	virtual bool HasMenu(int slot) = 0;

	// The handle of the menu `slot` has open, or kInvalidMenuHandle if none.
	virtual MenuHandle GetActiveMenu(int slot) = 0;

	// --- Lifetime ---

	// Free a menu. Any player currently viewing it has their display closed (fires MenuEnd=Destroyed).
	// The handle is invalid afterwards.
	//
	// IMPORTANT: a menu's callbacks may capture pointers into YOUR plugin.
	// Destroy every menu you created (and CancelMenu open displays) in your plugin's Unload()
	// so cs2menus never calls a lambda inside an unloaded DLL.
	virtual void DestroyMenu(MenuHandle menu) = 0;

	// --- Introspection (valid for live handles) ---

	virtual int GetItemCount(MenuHandle menu) = 0;
	// Returns the item's display text, or "" for an invalid handle/index.
	virtual const char *GetItemText(MenuHandle menu, int item) = 0;
	// Returns the item's info tag (see AddItem), or "" for an invalid handle/index.
	virtual const char *GetItemInfo(MenuHandle menu, int item) = 0;
};

#endif // _INCLUDE_ICS2MENUS_H_
