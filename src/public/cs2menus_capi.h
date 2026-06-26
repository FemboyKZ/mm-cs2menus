#ifndef _INCLUDE_CS2MENUS_CAPI_H_
#define _INCLUDE_CS2MENUS_CAPI_H_

#include <stdint.h>

// Flat C ABI over ICS2Menus002 (see ics2menus.h).
//
// Callback lifetime: a select/end callback is a function pointer into the host's managed runtime.
// If the host unloads/hot-reloads its plugin assembly while a menu still exists,
// cs2menus will call a freed pointer.
// The host wrapper MUST cs2m_destroy every handle it created on plugin unload. See the C# wrapper.

#if defined(_WIN32)
#define CS2M_CALL __cdecl
#if defined(CS2MENUS_EXPORTS)
#define CS2M_API extern "C" __declspec(dllexport)
#else
#define CS2M_API extern "C" __declspec(dllimport)
#endif
#else
#define CS2M_CALL
#define CS2M_API extern "C" __attribute__((visibility("default")))
#endif

// Bump when the C ABI below changes incompatibly. Hosts gate on this.
// New exports appended at the end are backward-compatible (old hosts simply don't call them).
#define CS2M_ABI_VERSION 1

// Mirrors MenuType / MenuEndReason / MenuButton / MenuNavAction (ics2menus.h),
// passed as plain int across the boundary.
//   type:   -1 Default, 0 Chat, 1 Html
//   reason:  0 Selected, 1 Exit, 2 Timeout, 3 Disconnect, 4 Cancelled, 5 Destroyed
//   action:  0 Up, 1 Down, 2 Select, 3 Back
//   button:  0 Default, 1..13 W/A/S/D/Use/Speed/Duck/Jump/Reload/Attack/Attack2/Score/Inspect, 14 None
//   label:   0 Exit, 1 NextPage, 2 PrevPage, 3 Move, 4 Scroll, 5 Select
//   style:   0 TitleColor, 1 TitleSize, 2 ItemSize, 3 NavColor, 4 FooterColor, 5 DisabledColor, 6 Align,
//            7 FontFace, 8 ItemColor, 9 Marker, 10 CounterColor, 11 ShowCounter, 12 FooterSize, 13 ShowFooter,
//            14 SubmenuSuffix, 15 FooterSeparator, 16 CounterPrefix, 17 CounterSuffix, 18 HighlightText

typedef uint32_t cs2m_handle; // 0 = invalid

// Fired when a player selects an item. `item` is the absolute index.
typedef void(CS2M_CALL *cs2m_select_cb)(cs2m_handle menu, int slot, int item, void *user);
// Fired exactly once when a player's display ends, for any reason (`reason` above).
typedef void(CS2M_CALL *cs2m_end_cb)(cs2m_handle menu, int slot, int reason, void *user);

// --- Handshake ---

// CS2M_ABI_VERSION the loaded library was built with. Gate before any other call.
CS2M_API int CS2M_CALL cs2m_abi_version(void);
// 1 if the underlying ICS2Menus002 instance is reachable.
// Reserved for future out-of-DLL acquisition, currently always 1 when the symbol resolves.
CS2M_API int CS2M_CALL cs2m_available(void);

// --- Build ---

// `on_select` may be null.
// `user` is echoed back to on_select and to the end callback set via cs2m_set_end_callback.
// Returns 0 on failure.
CS2M_API cs2m_handle CS2M_CALL cs2m_create(int type, const char *title, cs2m_select_cb on_select, void *user);
CS2M_API int CS2M_CALL cs2m_add_item(cs2m_handle menu, const char *text, const char *info, int disabled);
CS2M_API void CS2M_CALL cs2m_set_title(cs2m_handle menu, const char *title);
CS2M_API void CS2M_CALL cs2m_set_exit_button(cs2m_handle menu, int enabled);
CS2M_API void CS2M_CALL cs2m_set_close_on_select(cs2m_handle menu, int enabled);
CS2M_API void CS2M_CALL cs2m_set_end_callback(cs2m_handle menu, cs2m_end_cb on_end, void *user);
CS2M_API void CS2M_CALL cs2m_set_exit_item(cs2m_handle menu, int enabled);
CS2M_API void CS2M_CALL cs2m_set_menu_key(cs2m_handle menu, int action, int button);
// Rename a built-in label (see `label` values above). "" restores the configured default.
CS2M_API void CS2M_CALL cs2m_set_menu_label(cs2m_handle menu, int label, const char *text);
// Read a built-in label's current key (last set, or the default). Buffer semantics like cs2m_get_item_text.
CS2M_API int CS2M_CALL cs2m_get_menu_label(cs2m_handle menu, int label, char *buf, int buflen);
CS2M_API void CS2M_CALL cs2m_set_start_item(cs2m_handle menu, int item);
CS2M_API int CS2M_CALL cs2m_add_submenu(cs2m_handle parent, const char *text, cs2m_handle child, const char *info);
// Override one HTML style field (see `style` values above). "" inherits the server default.
// Sizes take a token ("s" "sm" "m" "ml" "l"), colors "#RRGGBB", Centered "1"/"0".
CS2M_API void CS2M_CALL cs2m_set_menu_style(cs2m_handle menu, int field, const char *value);
// Read a style field's effective value. Buffer semantics like cs2m_get_item_text.
CS2M_API int CS2M_CALL cs2m_get_menu_style(cs2m_handle menu, int field, char *buf, int buflen);

// --- Show / hide ---

CS2M_API int CS2M_CALL cs2m_display(cs2m_handle menu, int slot, float duration);
CS2M_API void CS2M_CALL cs2m_display_to_all(cs2m_handle menu, float duration);
CS2M_API void CS2M_CALL cs2m_cancel(int slot);
CS2M_API int CS2M_CALL cs2m_has_menu(int slot);
CS2M_API cs2m_handle CS2M_CALL cs2m_get_active_menu(int slot);
CS2M_API int CS2M_CALL cs2m_get_active_type(int slot);
CS2M_API int CS2M_CALL cs2m_get_selected_item(int slot);

// --- Host coordination ---

// Yield (busy=1) or reclaim (busy=0) a slot for a host menu system.
// While busy, cs2menus cancels any menu on that slot and refuses new displays,
// so it never fights the host for chat input or the center-HTML channel.
// The host drives this off its own menu open/close. cs2menus never auto-reopens.
CS2M_API void CS2M_CALL cs2m_set_external_busy(int slot, int busy);
CS2M_API int CS2M_CALL cs2m_get_external_busy(int slot);

// --- Lifetime ---

CS2M_API void CS2M_CALL cs2m_destroy(cs2m_handle menu);

// --- Introspection / live mutation ---

CS2M_API int CS2M_CALL cs2m_item_count(cs2m_handle menu);
// Copies UTF-8 text (NUL-terminated) into buf.
// Returns bytes needed including the NUL (>buflen means truncated).
// Pass buf=null/buflen=0 to query the size first.
CS2M_API int CS2M_CALL cs2m_get_item_text(cs2m_handle menu, int item, char *buf, int buflen);
CS2M_API int CS2M_CALL cs2m_get_item_info(cs2m_handle menu, int item, char *buf, int buflen);
CS2M_API void CS2M_CALL cs2m_set_item_text(cs2m_handle menu, int item, const char *text);
CS2M_API void CS2M_CALL cs2m_set_item_info(cs2m_handle menu, int item, const char *info);
CS2M_API void CS2M_CALL cs2m_set_item_disabled(cs2m_handle menu, int item, int disabled);
CS2M_API int CS2M_CALL cs2m_get_item_disabled(cs2m_handle menu, int item);
// HTML menus: render an item's text as raw Panorama markup (unescaped). See ICS2Menus::SetItemRaw.
CS2M_API void CS2M_CALL cs2m_set_item_raw(cs2m_handle menu, int item, int raw);
CS2M_API int CS2M_CALL cs2m_get_item_raw(cs2m_handle menu, int item);
// HTML menus: show an image before the item's text (icon URL / packaged path). "" removes it.
CS2M_API void CS2M_CALL cs2m_set_item_icon(cs2m_handle menu, int item, const char *url);
CS2M_API int CS2M_CALL cs2m_get_item_icon(cs2m_handle menu, int item, char *buf, int buflen);
CS2M_API void CS2M_CALL cs2m_remove_item(cs2m_handle menu, int item);
CS2M_API void CS2M_CALL cs2m_remove_all_items(cs2m_handle menu);
CS2M_API int CS2M_CALL cs2m_get_start_item(cs2m_handle menu);

#endif // _INCLUDE_CS2MENUS_CAPI_H_
