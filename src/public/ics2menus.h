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
// cs2menus owns player chat input: while a player has an open menu,
// it intercepts their "say"/"say_team" numeric input, drives the menu, and suppresses the chat line.
// Consumers only build menus and react to selections.
//
// Threading: every method is safe from the main (game) thread or a worker thread.
//  - Build/query calls (CreateMenu, AddItem, SetX, GetX...) run inline under a lock.
//  - DisplayMenu/CancelMenu off-thread are queued for the next GameFrame,
//    DisplayMenu then returns true optimistically.
//  - onSelect/onEnd callbacks always fire on the main thread.
//  - DestroyMenu off-thread invalidates the handle at once but skips the Destroyed callback.
//  - GetItemText/GetItemInfo pointers alias internal storage, copy them, don't cache.
//  - Don't block a main-thread callback on a worker that re-enters this API (lock is held -> deadlock).
#define CS2MENUS_INTERFACE "ICS2Menus002"

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
	Inspect, // F (look at weapon)
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

// Built-in text that SetMenuLabel can rename per menu.
enum class MenuLabel : int
{
	Exit = 0, // exit row / footer hint
	NextPage, // chat next-page row
	PrevPage, // chat previous-page row
	Move,     // HTML footer, shown when both up and down are bound
	Scroll,   // HTML footer, shown when only one of up/down is bound
	Select,   // HTML footer select hint
	Count,    // label count, not a valid argument
};

// Per-menu HTML style fields settable via SetMenuStyle.
// Each overrides the matching server default for this one menu.
// Pass "" to clear the override (inherit).
// HTML menus only, ignored for chat menus.
enum class MenuStyle : int
{
	TitleColor = 0,  // hex "#RRGGBB" for the title line
	TitleSize,       // size token: "xs" "s" "sm" "m" "ml" "l" "xl" "xxl" "xxxl"
	ItemSize,        // size token for item + cursor rows
	NavColor,        // hex for the cursor row + marker
	FooterColor,     // hex for the key-hint footer
	DisabledColor,   // hex for greyed-out items
	Align,           // line alignment: "left" / "center" / "right"
	FontFace,        // Panorama classes applied to every line, space-separated. "" = game default.
					 // Faces: stratum-{thin,light,regular,medium,bold,black}[-italic/-condensed],
					 // mono: stratum-{light,regular,bold}-mono / mono-spaced-font[-bold].
					 // Effects stack too: text-uppercase, text-letterspace-2px, text-shadow-basic.
					 // e.g. "stratum-bold text-uppercase text-shadow-basic".
	ItemColor,       // hex for normal (unselected, enabled) item text
	Marker,          // literal text drawn before the cursor row (default "▶ ")
	CounterColor,    // hex for the "[n/m]" position counter
	ShowCounter,     // "1" show the "[n/m]" counter on the title line, "0" hide it
	FooterSize,      // size token for the key-hint footer
	ShowFooter,      // "1" show the key-hint footer, "0" hide it
	SubmenuSuffix,   // text appended to items that open a submenu (default " >"). A space = none
	FooterSeparator, // text between footer hint segments (default " | ")
	CounterPrefix,   // text before the counter number (default "[")
	CounterSuffix,   // text after the counter number (default "]")
	HighlightText,   // "1" recolor the cursor row's text to NavColor, "0" only the marker marks it
	CounterSize,     // size token for the "[n/m]" position counter
	VisibleItems,    // integer string (e.g. "10"): rows shown at once in the scroll window
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
	// Called off-thread it returns true once the display is queued,
	// which is not a guarantee it will show (see the threading note above).
	virtual bool DisplayMenu(MenuHandle menu, int slot, float duration) = 0;

	// Close whatever menu `slot` has open (fires MenuEnd=Cancelled). No-op if none.
	virtual void CancelMenu(int slot) = 0;

	// True if `slot` currently has any menu open.
	virtual bool HasMenu(int slot) = 0;

	// The handle of the menu `slot` has open, or kInvalidMenuHandle if none.
	virtual MenuHandle GetActiveMenu(int slot) = 0;

	// The render type of the menu `slot` has open, or MenuType::Chat if none.
	// Lets a consumer tell whether the center-screen channel is in use (Html),
	// e.g. to yield its own HUD only for HTML menus, not chat ones.
	virtual MenuType GetActiveMenuType(int slot) = 0;

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

	// --- Live mutation ---

	// Change an item's display text in place. Any player viewing the menu is re-rendered.
	virtual void SetItemText(MenuHandle menu, int item, const char *text) = 0;

	// Grey/un-grey an item in place. Any player viewing the menu is re-rendered.
	virtual void SetItemDisabled(MenuHandle menu, int item, bool disabled) = 0;

	// Remove one item by absolute index. Shifts later indices down.
	// Viewers are re-rendered, their cursor/page clamped to the new size.
	virtual void RemoveItem(MenuHandle menu, int item) = 0;

	// Clear every item. Viewers are re-rendered (cursor/page reset to 0).
	virtual void RemoveAllItems(MenuHandle menu) = 0;

	// Item the menu opens on: HTML cursor row, or the chat page containing it.
	// Clamped to the item range when displayed. Default 0.
	virtual void SetStartItem(MenuHandle menu, int item) = 0;

	// Display `menu` to every connected player for `duration` seconds (0 = no timeout).
	// Each player's existing menu is replaced (fires its MenuEnd=Cancelled).
	// Safe off-thread: deferred to the next GameFrame.
	virtual void DisplayMenuToAll(MenuHandle menu, float duration) = 0;

	// Abs index of the item a player currently has highlighted in an HTML menu,
	// or -1 if they have no menu / it's a chat menu / the Exit row is highlighted.
	virtual int GetSelectedItem(int slot) = 0;

	// Change an item's info tag in place (see AddItem). No re-render needed.
	virtual void SetItemInfo(MenuHandle menu, int item, const char *info) = 0;

	// True if the item is greyed out. False for an invalid handle/index.
	virtual bool GetItemDisabled(MenuHandle menu, int item) = 0;

	// The configured start item (see SetStartItem), or 0 for an invalid handle.
	virtual int GetStartItem(MenuHandle menu) = 0;

	// Append an item that opens a submenu when selected, instead of firing onSelect.
	// Selecting it navigates into `child`. In the child, the Back key returns to this parent.
	// The parent's onSelect is not called for this item. Returns the new item's index, or -1 on failure.
	// `child` must be a live handle distinct from `parent`.
	virtual int AddSubMenu(MenuHandle parent, const char *text, MenuHandle child, const char *info) = 0;

	// --- Host coordination ---

	// Yield a slot to another menu system (e.g. a managed SwiftlyS2 / CS# menu).
	// While busy, any cs2menus menu on the slot is cancelled and further DisplayMenu calls for it are refused,
	// so cs2menus won't fight for chat input or the center-HTML channel.
	// The caller drives this off the other system's menu open/close.
	// cs2menus never auto-reopens when cleared.
	// Pairs with GetActiveMenuType/HasMenu so the other system can yield in turn.
	virtual void SetExternalBusy(int slot, bool busy) = 0;

	// Whether `slot` is currently yielded to an external menu system.
	virtual bool GetExternalBusy(int slot) = 0;

	// Rename one built-in label for this menu (Exit, page nav, footer hints).
	// Pass "" to restore the server-configured default. See MenuLabel.
	virtual void SetMenuLabel(MenuHandle menu, MenuLabel label, const char *text) = 0;

	// This menu's current label key for `label` (the value last set, or the built-in default).
	// It's a phrase key / literal, not the translated text.
	// Aliases internal storage, copy it, don't cache. Returns "" for an invalid handle/label.
	virtual const char *GetMenuLabel(MenuHandle menu, MenuLabel label) = 0;

	// Override one HTML style field for this menu (see MenuStyle). Pass "" to inherit the server default.
	// Re-renders any player currently viewing the menu. No-op for chat menus.
	virtual void SetMenuStyle(MenuHandle menu, MenuStyle field, const char *value) = 0;

	// This menu's effective value for a style field (the override if set, else the server default).
	// Sizes come back as the token, colors as "#RRGGBB", toggles (ShowCounter/ShowFooter/HighlightText) as "1"/"0".
	// Aliases internal storage, copy it, don't cache. Returns "" for an invalid handle/field.
	virtual const char *GetMenuStyle(MenuHandle menu, MenuStyle field) = 0;

	// HTML menus: mark an item's text as raw markup instead of plain text (default off).
	// Raw text is NOT escaped and chat color codes are NOT translated, so it can embed
	// Panorama HTML directly, e.g. "<img src='https://.../icon.png'> Rank".
	// The item is still wrapped in the row's size/face/color, which the raw markup may override.
	// You own the markup: keep it well-formed or it can break the panel. No-op for chat menus.
	virtual void SetItemRaw(MenuHandle menu, int item, bool raw) = 0;

	// True if the item is flagged as raw markup. False for an invalid handle/index.
	virtual bool GetItemRaw(MenuHandle menu, int item) = 0;

	// HTML menus: show an image just before the item's text, e.g. a rank or role icon.
	// `url` is a Panorama image source: an "http(s)://..." URL or a packaged material path.
	// Composes with the normal (escaped) item text, so you don't need SetItemRaw for a plain
	// "icon + label" row. Pass "" to remove it. No-op for chat menus.
	virtual void SetItemIcon(MenuHandle menu, int item, const char *url) = 0;

	// The item's icon URL (see SetItemIcon), or "" if none / invalid handle.
	// Aliases internal storage, copy it, don't cache.
	virtual const char *GetItemIcon(MenuHandle menu, int item) = 0;

	// By default a menu's render type (chat vs html) yields to the viewing player's saved preference,
	// when the server has per-player preferences enabled.
	// Call this with `force` = true to lock this menu to the type it was created with, so the player preference can't change it.
	// Use it when the menu depends on a specific type, e.g. HTML-only item icons or raw markup.
	// Default: not forced.
	virtual void SetMenuForceType(MenuHandle menu, bool force) = 0;

	// The menu's title as last set (a phrase key / literal, not translated text).
	// Aliases internal storage, copy it, don't cache. Returns "" for an invalid handle.
	virtual const char *GetTitle(MenuHandle menu) = 0;

	// True if `menu` is a live handle (created and not yet destroyed).
	virtual bool IsValidMenu(MenuHandle menu) = 0;

	// Insert an item at absolute index `pos` (clamped to [0, count]); later items shift down.
	// Any player viewing the menu is re-rendered (cursor/page clamped to the new size).
	// Returns the index it landed at, or -1 on failure.
	virtual int InsertItem(MenuHandle menu, int pos, const char *text, const char *info, bool disabled) = 0;

	// The child menu an item opens as a submenu (see AddSubMenu),
	// or kInvalidMenuHandle if it's a normal item / invalid handle or index.
	virtual MenuHandle GetItemSubmenu(MenuHandle menu, int item) = 0;

	// Make an existing item open `child` as a submenu (pass kInvalidMenuHandle to detach it).
	// Sets child's parent so Back in the child returns to this menu.
	// No-op for an invalid handle/index or child == menu.
	virtual void SetItemSubmenu(MenuHandle menu, int item, MenuHandle child) = 0;

	// The menu's base render type as created (may be MenuType::Default, meaning "server default").
	// For the per-viewer resolved type of an open display use GetActiveMenuType.
	// Returns MenuType::Default for an invalid handle.
	virtual MenuType GetMenuType(MenuHandle menu) = 0;

	// Read back the per-menu flags (the value last set, or the create-time default).
	// All return false for an invalid handle.
	virtual bool GetExitButton(MenuHandle menu) = 0;
	virtual bool GetCloseOnSelect(MenuHandle menu) = 0;
	virtual bool GetExitItem(MenuHandle menu) = 0;
	virtual bool GetMenuForceType(MenuHandle menu) = 0;

	// The per-menu nav-key override for one action (see SetMenuKey):
	// MenuButton::Default when none is set (inheriting the server binding),
	// MenuButton::None when the action is disabled for this menu.
	virtual MenuButton GetMenuKey(MenuHandle menu, MenuNavAction action) = 0;
};

#endif // _INCLUDE_ICS2MENUS_H_
