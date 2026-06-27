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
#define CS2MENUS_INTERFACE "ICS2Menus003"

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
	// --- Global / layout ---
	Align = 0,    // line alignment: "left" / "center" / "right"
	FontFace,     // Panorama classes applied to every line, space-separated. "" = game default.
				  // Faces: stratum-{thin,light,regular,medium,bold,black}[-italic/-condensed],
				  // mono: stratum-{light,regular,bold}-mono / mono-spaced-font[-bold].
				  // Effects stack too: text-uppercase, text-letterspace-2px, text-shadow-basic.
				  // e.g. "stratum-bold text-uppercase text-shadow-basic".
	VisibleItems, // integer string (e.g. "10"): rows shown at once in the scroll window

	// --- Title ---
	TitleColor, // hex "#RRGGBB" for the title line
	TitleSize,  // size token: "xs" "s" "sm" "m" "ml" "l" "xl" "xxl" "xxxl"
	RawTitle,   // "1" render the title as raw Panorama markup (unescaped, like SetItemRaw), "0" plain text

	// --- Items ---
	ItemColor,     // hex for normal (unselected, enabled) item text
	ItemSize,      // size token for item + cursor rows
	DisabledColor, // hex for greyed-out items
	SubmenuSuffix, // text appended to items that open a submenu (default " >"). A space = none

	// --- Cursor row ---
	NavColor,      // hex for the cursor row + marker
	Marker,        // literal text drawn before the cursor row (default "▶ ")
	HighlightText, // "1" recolor the cursor row's text to NavColor, "0" only the marker marks it

	// --- Position counter "[n/m]" ---
	ShowCounter,   // "1" show the counter on the title line, "0" hide it
	CounterColor,  // hex for the counter
	CounterSize,   // size token for the counter
	CounterFormat, // template, placeholders {cur} {total} (default "[{cur}/{total}]")

	// --- Key-hint footer ---
	ShowFooter,        // "1" show the footer, "0" hide it
	FooterColor,       // hex for the footer
	FooterSize,        // size token for the footer
	FooterSeparator,   // text between footer hint segments (default " | ")
	FooterHintFormat,  // one footer hint, placeholders {label} {keys} (default "{label}: {keys}")
	FooterRangeFormat, // the two keys in the Move hint, placeholders {up} {down} (default "{up}/{down}")
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
	// ============================== Lifetime ==============================

	// Create an empty menu of `type` (pass MenuType::Default to use the server's configured style).
	// `title` may contain chat color codes.
	// `onSelect` is invoked when a player picks an item (may be null if you only care about MenuEnd).
	// Returns kInvalidMenuHandle on failure.
	virtual MenuHandle CreateMenu(MenuType type, const char *title, MenuItemSelectFn onSelect) = 0;

	// Free a menu. Any player currently viewing it has their display closed (fires MenuEnd=Destroyed).
	// The handle is invalid afterwards.
	//
	// IMPORTANT: a menu's callbacks may capture pointers into YOUR plugin.
	// Destroy every menu you created (and CancelMenu open displays) in your plugin's Unload()
	// so cs2menus never calls a lambda inside an unloaded DLL.
	virtual void DestroyMenu(MenuHandle menu) = 0;

	// True if `menu` is a live handle (created and not yet destroyed).
	virtual bool IsValidMenu(MenuHandle menu) = 0;

	// ========================== Menu properties ==========================
	// Each Set re-renders any player currently viewing the menu where it matters.
	// Each Get returns the value last set (create-time default if never set), or a zero value for an invalid handle.

	// Title text (may contain chat color codes).
	// GetTitle aliases internal storage, copy it, don't cache; "" for an invalid handle.
	virtual void SetTitle(MenuHandle menu, const char *title) = 0;
	virtual const char *GetTitle(MenuHandle menu) = 0;

	// The menu's base render type as created (may be MenuType::Default = "server default"). Set at CreateMenu.
	// For the per-viewer resolved type of an open display use GetActiveMenuType. Default for an invalid handle.
	virtual MenuType GetMenuType(MenuHandle menu) = 0;

	// Show/hide the trailing "0. Exit" entry (default: shown).
	virtual void SetExitButton(MenuHandle menu, bool enabled) = 0;
	virtual bool GetExitButton(MenuHandle menu) = 0;

	// Close the menu automatically after a selection (default: true).
	// When false, the menu is re-rendered after each pick so the player can choose again.
	virtual void SetCloseOnSelect(MenuHandle menu, bool enabled) = 0;
	virtual bool GetCloseOnSelect(MenuHandle menu) = 0;

	// HTML menus: show a selectable "Exit" row at the end of the list (default off).
	// Useful when the Back key is set to None, it's also auto-shown in that case so a
	// menu is never left unexitable. Requires SetExitButton(true). No-op for chat menus.
	virtual void SetExitItem(MenuHandle menu, bool enabled) = 0;
	virtual bool GetExitItem(MenuHandle menu) = 0;

	// Lock this menu's render type so the viewing player's saved type preference can't change it.
	// Use it when the menu depends on a specific type, e.g. HTML-only item icons or raw markup. Default: not forced.
	virtual void SetMenuForceType(MenuHandle menu, bool force) = 0;
	virtual bool GetMenuForceType(MenuHandle menu) = 0;

	// Item the menu opens on: HTML cursor row, or the chat page containing it.
	// Clamped to the item range when displayed. Default 0.
	virtual void SetStartItem(MenuHandle menu, int item) = 0;
	virtual int GetStartItem(MenuHandle menu) = 0;

	// Register the per-menu end callback. See MenuEndFn. (No getter: callbacks aren't introspectable.)
	virtual void SetMenuEndCallback(MenuHandle menu, MenuEndFn onEnd) = 0;

	// Override the HTML navigation key for one action.
	// MenuButton::Default clears the override (inherit the server binding).
	// MenuButton::None disables the action for this menu (e.g. disable Up so one key cycles, the cursor wraps).
	// The footer key hints update to match.
	// GetMenuKey returns the per-menu override: Default if unset, None if disabled.
	virtual void SetMenuKey(MenuHandle menu, MenuNavAction action, MenuButton button) = 0;
	virtual MenuButton GetMenuKey(MenuHandle menu, MenuNavAction action) = 0;

	// Rename one built-in label for this menu (Exit, page nav, footer hints). See MenuLabel.
	// Pass "" to restore the server-configured default.
	// GetMenuLabel returns the current key (a phrase key / literal, not translated text);
	// aliases internal storage, copy it, don't cache. "" for an invalid handle/label.
	virtual void SetMenuLabel(MenuHandle menu, MenuLabel label, const char *text) = 0;
	virtual const char *GetMenuLabel(MenuHandle menu, MenuLabel label) = 0;

	// Override one HTML style field for this menu (see MenuStyle). Pass "" to inherit the server default.
	// No-op for chat menus.
	// GetMenuStyle returns the effective value (override if set, else server default): sizes as the token,
	// colors as "#RRGGBB", toggles as "1"/"0", templates as the format string.
	// Aliases internal storage, copy it, don't cache. "" for an invalid handle/field.
	virtual void SetMenuStyle(MenuHandle menu, MenuStyle field, const char *value) = 0;
	virtual const char *GetMenuStyle(MenuHandle menu, MenuStyle field) = 0;

	// ================================ Items ===============================

	// Append an item. Returns the new item's absolute index, or -1 on failure.
	// `info` is an opaque tag echoed back via GetItemInfo, pass "" if unused.
	// A `disabled` item is shown greyed out and cannot be selected.
	virtual int AddItem(MenuHandle menu, const char *text, const char *info, bool disabled) = 0;

	// Insert an item at absolute index `pos` (clamped to [0, count]); later items shift down.
	// Viewers are re-rendered (cursor/page clamped to the new size). Returns the index, or -1.
	virtual int InsertItem(MenuHandle menu, int pos, const char *text, const char *info, bool disabled) = 0;

	// Append an item that opens a submenu when selected, instead of firing onSelect.
	// In the child, the Back key returns to this parent. Returns the new item's index, or -1.
	// `child` must be a live handle distinct from `parent`.
	virtual int AddSubMenu(MenuHandle parent, const char *text, MenuHandle child, const char *info) = 0;

	// Remove one item by absolute index (shifts later indices down). Viewers re-rendered, cursor/page clamped.
	virtual void RemoveItem(MenuHandle menu, int item) = 0;

	// Clear every item. Viewers are re-rendered (cursor/page reset to 0).
	virtual void RemoveAllItems(MenuHandle menu) = 0;

	// Number of items in the menu, or 0 for an invalid handle.
	virtual int GetItemCount(MenuHandle menu) = 0;

	// Item display text. SetItemText re-renders viewers.
	// GetItemText aliases internal storage, copy it, don't cache; "" for an invalid handle/index.
	virtual void SetItemText(MenuHandle menu, int item, const char *text) = 0;
	virtual const char *GetItemText(MenuHandle menu, int item) = 0;

	// Item info tag (opaque, see AddItem). No re-render.
	// GetItemInfo aliases internal storage, copy it, don't cache; "" for an invalid handle/index.
	virtual void SetItemInfo(MenuHandle menu, int item, const char *info) = 0;
	virtual const char *GetItemInfo(MenuHandle menu, int item) = 0;

	// Grey/un-grey an item (re-renders viewers). GetItemDisabled is false for an invalid handle/index.
	virtual void SetItemDisabled(MenuHandle menu, int item, bool disabled) = 0;
	virtual bool GetItemDisabled(MenuHandle menu, int item) = 0;

	// HTML menus: render the item's text as raw Panorama markup (unescaped, no chat-color translation),
	// so it can embed <img>/<font>/etc. The item still gets the row's size/face/color wrapper.
	// You own well-formedness. No-op for chat menus. GetItemRaw is false for an invalid handle/index.
	virtual void SetItemRaw(MenuHandle menu, int item, bool raw) = 0;
	virtual bool GetItemRaw(MenuHandle menu, int item) = 0;

	// HTML menus: show an image just before the item's text (e.g. a rank or role icon).
	// `url` is a Panorama image source ("http(s)://..." URL or a packaged material path). "" removes it.
	// Composes with the normal (escaped) text, so no SetItemRaw needed for "icon + label". No-op for chat menus.
	// GetItemIcon aliases internal storage, copy it; "" if none / invalid handle.
	virtual void SetItemIcon(MenuHandle menu, int item, const char *url) = 0;
	virtual const char *GetItemIcon(MenuHandle menu, int item) = 0;

	// Make an existing item open `child` as a submenu (kInvalidMenuHandle detaches).
	// Sets child's parent so Back in the child returns here. No-op for an invalid handle/index or child == menu.
	// GetItemSubmenu returns the child, or kInvalidMenuHandle if it's a normal item / invalid.
	virtual void SetItemSubmenu(MenuHandle menu, int item, MenuHandle child) = 0;
	virtual MenuHandle GetItemSubmenu(MenuHandle menu, int item) = 0;

	// =============================== Display ==============================

	// Display `menu` to `slot` for `duration` seconds (0 = no timeout).
	// Replaces any menu the player has open (firing its MenuEnd=Cancelled first).
	// Returns false for an invalid handle/slot.
	// Off-thread it returns true once the display is queued, which is not a guarantee it will show
	// (see the threading note above).
	virtual bool DisplayMenu(MenuHandle menu, int slot, float duration) = 0;

	// Display `menu` to every connected player for `duration` seconds (0 = no timeout).
	// Each player's existing menu is replaced (fires its MenuEnd=Cancelled). Safe off-thread.
	virtual void DisplayMenuToAll(MenuHandle menu, float duration) = 0;

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

	// Abs index of the item a player currently has highlighted in an HTML menu,
	// or -1 if they have no menu / it's a chat menu / the Exit row is highlighted.
	virtual int GetSelectedItem(int slot) = 0;

	// ========================= Host coordination =========================

	// Yield a slot to another menu system (e.g. a managed SwiftlyS2 / CS# menu).
	// While busy, any cs2menus menu on the slot is cancelled and further DisplayMenu calls for it are refused,
	// so cs2menus won't fight for chat input or the center-HTML channel.
	// The caller drives this off the other system's menu open/close. cs2menus never auto-reopens.
	// Pairs with GetActiveMenuType/HasMenu so the other system can yield in turn.
	virtual void SetExternalBusy(int slot, bool busy) = 0;
	virtual bool GetExternalBusy(int slot) = 0;
};

#endif // _INCLUDE_ICS2MENUS_H_
