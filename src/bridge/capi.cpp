// Flat C ABI over ICS2Menus005 for managed hosts (SwiftlyS2, CounterStrikeSharp).
// See mm-utils' interfaces/cs2menus/cs2menus_capi.h for the contract.

#define CS2MENUS_EXPORTS
#include "interfaces/cs2menus/cs2menus_capi.h"
#include "interfaces/cs2menus/ics2menus.h"

#include <cstring>

// Defined in cs2menus.cpp: the singleton ICS2Menus005 implementation.
// Routing through it inherits the curtime stamping and off-thread queueing the interface wrapper already does.
extern ICS2Menus *Cs2Menus_GetLocalAPI();

namespace
{
	ICS2Menus *API()
	{
		return Cs2Menus_GetLocalAPI();
	}

	// Copy a (possibly null) C string into a caller buffer, at once: the getters' pointers alias live menu storage.
	// Returns bytes needed including the NUL terminator. Always NUL-terminates when buflen > 0.
	int CopyOut(const char *src, char *buf, int buflen)
	{
		if (!src)
		{
			src = "";
		}
		int needed = static_cast<int>(std::strlen(src)) + 1;
		if (buf && buflen > 0)
		{
			int n = needed < buflen ? needed : buflen;
			std::memcpy(buf, src, n - 1);
			buf[n - 1] = '\0';
		}
		return needed;
	}
} // namespace

// --- Handshake ---

CS2M_API int CS2M_CALL cs2m_abi_version(void)
{
	return CS2M_ABI_VERSION;
}

CS2M_API int CS2M_CALL cs2m_available(void)
{
	return 1;
}

// --- Lifetime ---

CS2M_API cs2m_handle CS2M_CALL cs2m_create(int type, const char *title, cs2m_select_cb on_select, void *user)
{
	MenuItemSelectFn fn;
	if (on_select)
	{
		// Capture the raw pointer + token, not a managed object:
		// the host owns lifetime and must cs2m_destroy before its assembly unloads.
		fn = [on_select, user](MenuHandle menu, int slot, int item) { on_select(menu, slot, item, user); };
	}
	return API()->CreateMenu(static_cast<MenuType>(type), title ? title : "", std::move(fn));
}

CS2M_API void CS2M_CALL cs2m_destroy(cs2m_handle menu)
{
	API()->DestroyMenu(menu);
}

CS2M_API int CS2M_CALL cs2m_is_valid(cs2m_handle menu)
{
	return API()->IsValidMenu(menu) ? 1 : 0;
}

// --- Menu properties ---

CS2M_API void CS2M_CALL cs2m_set_title(cs2m_handle menu, const char *title)
{
	API()->SetTitle(menu, title ? title : "");
}

CS2M_API int CS2M_CALL cs2m_get_title(cs2m_handle menu, char *buf, int buflen)
{
	return CopyOut(API()->GetTitle(menu), buf, buflen);
}

CS2M_API int CS2M_CALL cs2m_get_menu_type(cs2m_handle menu)
{
	return static_cast<int>(API()->GetMenuType(menu));
}

CS2M_API void CS2M_CALL cs2m_set_exit_button(cs2m_handle menu, int enabled)
{
	API()->SetExitButton(menu, enabled != 0);
}

CS2M_API int CS2M_CALL cs2m_get_exit_button(cs2m_handle menu)
{
	return API()->GetExitButton(menu) ? 1 : 0;
}

CS2M_API void CS2M_CALL cs2m_set_close_on_select(cs2m_handle menu, int enabled)
{
	API()->SetCloseOnSelect(menu, enabled != 0);
}

CS2M_API int CS2M_CALL cs2m_get_close_on_select(cs2m_handle menu)
{
	return API()->GetCloseOnSelect(menu) ? 1 : 0;
}

CS2M_API void CS2M_CALL cs2m_set_exit_item(cs2m_handle menu, int enabled)
{
	API()->SetExitItem(menu, enabled != 0);
}

CS2M_API int CS2M_CALL cs2m_get_exit_item(cs2m_handle menu)
{
	return API()->GetExitItem(menu) ? 1 : 0;
}

CS2M_API void CS2M_CALL cs2m_set_force_type(cs2m_handle menu, int force)
{
	API()->SetMenuForceType(menu, force != 0);
}

CS2M_API int CS2M_CALL cs2m_get_force_type(cs2m_handle menu)
{
	return API()->GetMenuForceType(menu) ? 1 : 0;
}

CS2M_API void CS2M_CALL cs2m_set_start_item(cs2m_handle menu, int item)
{
	API()->SetStartItem(menu, item);
}

CS2M_API int CS2M_CALL cs2m_get_start_item(cs2m_handle menu)
{
	return API()->GetStartItem(menu);
}

CS2M_API void CS2M_CALL cs2m_set_end_callback(cs2m_handle menu, cs2m_end_cb on_end, void *user)
{
	MenuEndFn fn;
	if (on_end)
	{
		fn = [on_end, user](MenuHandle m, int slot, MenuEndReason reason) { on_end(m, slot, static_cast<int>(reason), user); };
	}
	API()->SetMenuEndCallback(menu, std::move(fn));
}

CS2M_API void CS2M_CALL cs2m_set_menu_key(cs2m_handle menu, int action, int button)
{
	API()->SetMenuKey(menu, static_cast<MenuNavAction>(action), static_cast<MenuButton>(button));
}

CS2M_API int CS2M_CALL cs2m_get_menu_key(cs2m_handle menu, int action)
{
	return static_cast<int>(API()->GetMenuKey(menu, static_cast<MenuNavAction>(action)));
}

CS2M_API void CS2M_CALL cs2m_set_menu_label(cs2m_handle menu, int label, const char *text)
{
	API()->SetMenuLabel(menu, static_cast<MenuLabel>(label), text ? text : "");
}

CS2M_API int CS2M_CALL cs2m_get_menu_label(cs2m_handle menu, int label, char *buf, int buflen)
{
	return CopyOut(API()->GetMenuLabel(menu, static_cast<MenuLabel>(label)), buf, buflen);
}

CS2M_API void CS2M_CALL cs2m_set_menu_style(cs2m_handle menu, int field, const char *value)
{
	API()->SetMenuStyle(menu, static_cast<MenuStyle>(field), value ? value : "");
}

CS2M_API int CS2M_CALL cs2m_get_menu_style(cs2m_handle menu, int field, char *buf, int buflen)
{
	return CopyOut(API()->GetMenuStyle(menu, static_cast<MenuStyle>(field)), buf, buflen);
}

// --- Items ---

CS2M_API int CS2M_CALL cs2m_add_item(cs2m_handle menu, const char *text, const char *info, int disabled)
{
	return API()->AddItem(menu, text ? text : "", info ? info : "", disabled != 0);
}

CS2M_API int CS2M_CALL cs2m_insert_item(cs2m_handle menu, int pos, const char *text, const char *info, int disabled)
{
	return API()->InsertItem(menu, pos, text ? text : "", info ? info : "", disabled != 0);
}

CS2M_API int CS2M_CALL cs2m_add_submenu(cs2m_handle parent, const char *text, cs2m_handle child, const char *info)
{
	return API()->AddSubMenu(parent, text ? text : "", child, info ? info : "");
}

CS2M_API void CS2M_CALL cs2m_remove_item(cs2m_handle menu, int item)
{
	API()->RemoveItem(menu, item);
}

CS2M_API void CS2M_CALL cs2m_remove_all_items(cs2m_handle menu)
{
	API()->RemoveAllItems(menu);
}

CS2M_API int CS2M_CALL cs2m_item_count(cs2m_handle menu)
{
	return API()->GetItemCount(menu);
}

CS2M_API void CS2M_CALL cs2m_set_item_text(cs2m_handle menu, int item, const char *text)
{
	API()->SetItemText(menu, item, text ? text : "");
}

CS2M_API int CS2M_CALL cs2m_get_item_text(cs2m_handle menu, int item, char *buf, int buflen)
{
	return CopyOut(API()->GetItemText(menu, item), buf, buflen);
}

CS2M_API void CS2M_CALL cs2m_set_item_info(cs2m_handle menu, int item, const char *info)
{
	API()->SetItemInfo(menu, item, info ? info : "");
}

CS2M_API int CS2M_CALL cs2m_get_item_info(cs2m_handle menu, int item, char *buf, int buflen)
{
	return CopyOut(API()->GetItemInfo(menu, item), buf, buflen);
}

CS2M_API void CS2M_CALL cs2m_set_item_disabled(cs2m_handle menu, int item, int disabled)
{
	API()->SetItemDisabled(menu, item, disabled != 0);
}

CS2M_API int CS2M_CALL cs2m_get_item_disabled(cs2m_handle menu, int item)
{
	return API()->GetItemDisabled(menu, item) ? 1 : 0;
}

CS2M_API void CS2M_CALL cs2m_set_item_raw(cs2m_handle menu, int item, int raw)
{
	API()->SetItemRaw(menu, item, raw != 0);
}

CS2M_API int CS2M_CALL cs2m_get_item_raw(cs2m_handle menu, int item)
{
	return API()->GetItemRaw(menu, item) ? 1 : 0;
}

CS2M_API void CS2M_CALL cs2m_set_item_icon(cs2m_handle menu, int item, const char *url)
{
	API()->SetItemIcon(menu, item, url ? url : "");
}

CS2M_API int CS2M_CALL cs2m_get_item_icon(cs2m_handle menu, int item, char *buf, int buflen)
{
	return CopyOut(API()->GetItemIcon(menu, item), buf, buflen);
}

CS2M_API void CS2M_CALL cs2m_set_item_submenu(cs2m_handle menu, int item, cs2m_handle child)
{
	API()->SetItemSubmenu(menu, item, child);
}

CS2M_API cs2m_handle CS2M_CALL cs2m_get_item_submenu(cs2m_handle menu, int item)
{
	return API()->GetItemSubmenu(menu, item);
}

// --- Display ---

CS2M_API int CS2M_CALL cs2m_display(cs2m_handle menu, int slot, float duration)
{
	return API()->DisplayMenu(menu, slot, duration) ? 1 : 0;
}

CS2M_API void CS2M_CALL cs2m_display_to_all(cs2m_handle menu, float duration)
{
	API()->DisplayMenuToAll(menu, duration);
}

CS2M_API void CS2M_CALL cs2m_cancel(int slot)
{
	API()->CancelMenu(slot);
}

CS2M_API int CS2M_CALL cs2m_has_menu(int slot)
{
	return API()->HasMenu(slot) ? 1 : 0;
}

CS2M_API cs2m_handle CS2M_CALL cs2m_get_active_menu(int slot)
{
	return API()->GetActiveMenu(slot);
}

CS2M_API int CS2M_CALL cs2m_get_slot_type(int slot, int type)
{
	return static_cast<int>(API()->GetSlotMenuType(slot, static_cast<MenuType>(type)));
}

CS2M_API int CS2M_CALL cs2m_get_active_type(int slot)
{
	return static_cast<int>(API()->GetActiveMenuType(slot));
}

CS2M_API int CS2M_CALL cs2m_get_selected_item(int slot)
{
	return API()->GetSelectedItem(slot);
}

// --- Host coordination ---

CS2M_API void CS2M_CALL cs2m_set_external_busy(int slot, int busy)
{
	API()->SetExternalBusy(slot, busy != 0);
}

CS2M_API int CS2M_CALL cs2m_get_external_busy(int slot)
{
	return API()->GetExternalBusy(slot) ? 1 : 0;
}

// --- Value items ---

CS2M_API int CS2M_CALL cs2m_add_toggle(cs2m_handle menu, const char *text, int on, const char *info)
{
	return API()->AddToggle(menu, text ? text : "", on != 0, info ? info : "");
}

CS2M_API int CS2M_CALL cs2m_add_stepper(cs2m_handle menu, const char *text, int value, int min, int max, int step, const char *info)
{
	return API()->AddStepper(menu, text ? text : "", value, min, max, step, info ? info : "");
}

CS2M_API int CS2M_CALL cs2m_add_choice(cs2m_handle menu, const char *text, const char *const *options, int count, int selected, const char *info)
{
	return API()->AddChoice(menu, text ? text : "", options, options ? count : 0, selected, info ? info : "");
}

CS2M_API int CS2M_CALL cs2m_get_item_type(cs2m_handle menu, int item)
{
	return static_cast<int>(API()->GetItemType(menu, item));
}

CS2M_API void CS2M_CALL cs2m_set_item_value(cs2m_handle menu, int item, int value)
{
	API()->SetItemValue(menu, item, value);
}

CS2M_API int CS2M_CALL cs2m_get_item_value(cs2m_handle menu, int item)
{
	return API()->GetItemValue(menu, item);
}

CS2M_API void CS2M_CALL cs2m_set_change_callback(cs2m_handle menu, cs2m_change_cb on_change, void *user)
{
	MenuItemChangeFn fn;
	if (on_change)
	{
		fn = [on_change, user](MenuHandle m, int slot, int item, int value) { on_change(m, slot, item, value, user); };
	}
	API()->SetMenuChangeCallback(menu, std::move(fn));
}

// --- Sections and grids ---

CS2M_API int CS2M_CALL cs2m_add_section(cs2m_handle menu, const char *name)
{
	return API()->AddSection(menu, name ? name : "");
}

CS2M_API int CS2M_CALL cs2m_get_item_section(cs2m_handle menu, int item)
{
	return API()->GetItemSection(menu, item);
}

CS2M_API void CS2M_CALL cs2m_set_menu_layout(cs2m_handle menu, int layout)
{
	API()->SetMenuLayout(menu, static_cast<MenuLayout>(layout));
}

CS2M_API int CS2M_CALL cs2m_get_menu_layout(cs2m_handle menu)
{
	return static_cast<int>(API()->GetMenuLayout(menu));
}

CS2M_API void CS2M_CALL cs2m_set_item_image(cs2m_handle menu, int item, const char *image)
{
	API()->SetItemImage(menu, item, image ? image : "");
}

CS2M_API int CS2M_CALL cs2m_get_item_image(cs2m_handle menu, int item, char *buf, int buflen)
{
	return CopyOut(API()->GetItemImage(menu, item), buf, buflen);
}

CS2M_API void CS2M_CALL cs2m_set_item_subtext(cs2m_handle menu, int item, const char *subtext)
{
	API()->SetItemSubtext(menu, item, subtext ? subtext : "");
}

CS2M_API int CS2M_CALL cs2m_get_item_subtext(cs2m_handle menu, int item, char *buf, int buflen)
{
	return CopyOut(API()->GetItemSubtext(menu, item), buf, buflen);
}

// --- Panorama layouts ---

CS2M_API void CS2M_CALL cs2m_set_menu_tile_size(cs2m_handle menu, int size)
{
	API()->SetMenuTileSize(menu, static_cast<MenuTileSize>(size));
}

CS2M_API int CS2M_CALL cs2m_get_menu_tile_size(cs2m_handle menu)
{
	return static_cast<int>(API()->GetMenuTileSize(menu));
}

CS2M_API void CS2M_CALL cs2m_set_menu_image(cs2m_handle menu, const char *image)
{
	API()->SetMenuImage(menu, image ? image : "");
}

CS2M_API int CS2M_CALL cs2m_get_menu_image(cs2m_handle menu, char *buf, int buflen)
{
	return CopyOut(API()->GetMenuImage(menu), buf, buflen);
}

CS2M_API void CS2M_CALL cs2m_set_menu_pinned_item(cs2m_handle menu, int item)
{
	API()->SetMenuPinnedItem(menu, item);
}

CS2M_API int CS2M_CALL cs2m_get_menu_pinned_item(cs2m_handle menu)
{
	return API()->GetMenuPinnedItem(menu);
}

// --- History ---

CS2M_API int CS2M_CALL cs2m_push(cs2m_handle menu, int slot, float duration)
{
	return API()->PushMenu(menu, slot, duration) ? 1 : 0;
}

CS2M_API int CS2M_CALL cs2m_replace(cs2m_handle menu, int slot, float duration)
{
	return API()->ReplaceMenu(menu, slot, duration) ? 1 : 0;
}

CS2M_API int CS2M_CALL cs2m_step_back(int slot, int steps)
{
	return API()->StepBack(slot, steps) ? 1 : 0;
}

CS2M_API void CS2M_CALL cs2m_set_refresh_callback(cs2m_handle menu, cs2m_refresh_cb on_refresh, void *user)
{
	MenuRefreshFn fn;
	if (on_refresh)
	{
		fn = [on_refresh, user](MenuHandle m, int slot) { on_refresh(m, slot, user); };
	}
	API()->SetMenuRefreshCallback(menu, std::move(fn));
}

// --- Pausing a display ---

CS2M_API void CS2M_CALL cs2m_suspend(int slot)
{
	API()->SuspendMenu(slot);
}

CS2M_API void CS2M_CALL cs2m_resume(int slot)
{
	API()->ResumeMenu(slot);
}

// --- Item presentation ---

CS2M_API void CS2M_CALL cs2m_set_item_role(cs2m_handle menu, int item, int role)
{
	API()->SetItemRole(menu, item, static_cast<MenuItemRole>(role));
}

CS2M_API int CS2M_CALL cs2m_get_item_role(cs2m_handle menu, int item)
{
	return static_cast<int>(API()->GetItemRole(menu, item));
}

CS2M_API void CS2M_CALL cs2m_set_item_highlight(cs2m_handle menu, int item, int highlight)
{
	API()->SetItemHighlight(menu, item, highlight != 0);
}

CS2M_API void CS2M_CALL cs2m_set_item_span(cs2m_handle menu, int item, int columns)
{
	API()->SetItemSpan(menu, item, columns);
}

CS2M_API void CS2M_CALL cs2m_set_item_control(cs2m_handle menu, int item, int control)
{
	API()->SetItemControl(menu, item, control != 0);
}

// --- Tile badges ---

CS2M_API void CS2M_CALL cs2m_set_item_rarity(cs2m_handle menu, int item, const char *rarity)
{
	API()->SetItemRarity(menu, item, rarity ? rarity : "");
}

CS2M_API int CS2M_CALL cs2m_get_item_rarity(cs2m_handle menu, int item, char *buf, int buflen)
{
	return CopyOut(API()->GetItemRarity(menu, item), buf, buflen);
}

CS2M_API void CS2M_CALL cs2m_set_item_tag(cs2m_handle menu, int item, const char *tag, const char *style)
{
	API()->SetItemTag(menu, item, tag ? tag : "", style ? style : "");
}

CS2M_API int CS2M_CALL cs2m_get_item_tag(cs2m_handle menu, int item, char *buf, int buflen)
{
	return CopyOut(API()->GetItemTag(menu, item), buf, buflen);
}

CS2M_API void CS2M_CALL cs2m_set_item_teams(cs2m_handle menu, int item, int teams)
{
	API()->SetItemTeams(menu, item, teams);
}

CS2M_API int CS2M_CALL cs2m_get_item_teams(cs2m_handle menu, int item)
{
	return API()->GetItemTeams(menu, item);
}

CS2M_API void CS2M_CALL cs2m_set_item_locked(cs2m_handle menu, int item, int locked)
{
	API()->SetItemLocked(menu, item, locked != 0);
}

CS2M_API int CS2M_CALL cs2m_get_item_locked(cs2m_handle menu, int item)
{
	return API()->GetItemLocked(menu, item) ? 1 : 0;
}

CS2M_API void CS2M_CALL cs2m_set_item_corner(cs2m_handle menu, int item, int corner)
{
	API()->SetItemCorner(menu, item, static_cast<MenuCorner>(corner));
}

CS2M_API int CS2M_CALL cs2m_get_item_corner(cs2m_handle menu, int item)
{
	return static_cast<int>(API()->GetItemCorner(menu, item));
}

CS2M_API void CS2M_CALL cs2m_set_corner_callback(cs2m_handle menu, cs2m_corner_cb on_corner, void *user)
{
	MenuItemCornerFn fn;
	if (on_corner)
	{
		fn = [on_corner, user](MenuHandle m, int slot, int item) { on_corner(m, slot, item, user); };
	}
	API()->SetMenuCornerCallback(menu, std::move(fn));
}

CS2M_API void CS2M_CALL cs2m_set_item_image_tint(cs2m_handle menu, int item, const char *tint)
{
	API()->SetItemImageTint(menu, item, tint ? tint : "");
}

// --- Info card ---

CS2M_API void CS2M_CALL cs2m_set_menu_info(cs2m_handle menu, const char *title, const char *subtitle, const char *subtitle_color)
{
	API()->SetMenuInfo(menu, title ? title : "", subtitle ? subtitle : "", subtitle_color ? subtitle_color : "");
}

CS2M_API void CS2M_CALL cs2m_set_menu_info_meter(cs2m_handle menu, float value, float range_min, float range_max, const float *bands, int count,
												 const char *label, const char *value_text)
{
	API()->SetMenuInfoMeter(menu, value, range_min, range_max, bands, bands ? count : 0, label ? label : "", value_text ? value_text : "");
}

CS2M_API int CS2M_CALL cs2m_add_menu_info_row(cs2m_handle menu, const char *label, const char *value)
{
	return API()->AddMenuInfoRow(menu, label ? label : "", value ? value : "");
}

CS2M_API void CS2M_CALL cs2m_clear_menu_info(cs2m_handle menu)
{
	API()->ClearMenuInfo(menu);
}

// --- The header ---

CS2M_API void CS2M_CALL cs2m_set_menu_scope(cs2m_handle menu, const char *label, int teams)
{
	API()->SetMenuScope(menu, label ? label : "", teams);
}

CS2M_API void CS2M_CALL cs2m_set_scope_callback(cs2m_handle menu, cs2m_scope_cb on_scope, void *user)
{
	MenuScopeFn fn;
	if (on_scope)
	{
		fn = [on_scope, user](MenuHandle m, int slot) { on_scope(m, slot, user); };
	}
	API()->SetMenuScopeCallback(menu, std::move(fn));
}

CS2M_API void CS2M_CALL cs2m_set_menu_edited(cs2m_handle menu, int edited)
{
	API()->SetMenuEdited(menu, edited != 0);
}

CS2M_API int CS2M_CALL cs2m_get_menu_edited(cs2m_handle menu)
{
	return API()->GetMenuEdited(menu) ? 1 : 0;
}

// --- Tabs and chips ---

CS2M_API int CS2M_CALL cs2m_add_menu_tab(cs2m_handle menu, const char *label, int selected, int marked, int pinned)
{
	return API()->AddMenuTab(menu, label ? label : "", selected != 0, marked != 0, pinned != 0);
}

CS2M_API void CS2M_CALL cs2m_set_tab_callback(cs2m_handle menu, cs2m_tab_cb on_tab, void *user)
{
	MenuTabFn fn;
	if (on_tab)
	{
		fn = [on_tab, user](MenuHandle m, int slot, int tab) { on_tab(m, slot, tab, user); };
	}
	API()->SetMenuTabCallback(menu, std::move(fn));
}

CS2M_API int CS2M_CALL cs2m_add_menu_chip(cs2m_handle menu, const char *label, const char *const *options, int count, int selected)
{
	return API()->AddMenuChip(menu, label ? label : "", options, options ? count : 0, selected);
}

CS2M_API int CS2M_CALL cs2m_add_menu_action(cs2m_handle menu, const char *label, const char *const *options, int count, int accent)
{
	return API()->AddMenuAction(menu, label ? label : "", options, options ? count : 0, accent != 0);
}

CS2M_API int CS2M_CALL cs2m_add_menu_note(cs2m_handle menu, const char *label, const char *value)
{
	return API()->AddMenuNote(menu, label ? label : "", value ? value : "");
}

CS2M_API void CS2M_CALL cs2m_set_chip_option_tone(cs2m_handle menu, int chip, int option, int tone)
{
	API()->SetMenuChipOptionTone(menu, chip, option, static_cast<MenuTone>(tone));
}

CS2M_API void CS2M_CALL cs2m_set_chip_callback(cs2m_handle menu, cs2m_chip_cb on_chip, void *user)
{
	MenuChipFn fn;
	if (on_chip)
	{
		fn = [on_chip, user](MenuHandle m, int slot, int chip, int selected) { on_chip(m, slot, chip, selected, user); };
	}
	API()->SetMenuChipCallback(menu, std::move(fn));
}

// --- The item area ---

CS2M_API void CS2M_CALL cs2m_set_menu_secondary_item(cs2m_handle menu, int item)
{
	API()->SetMenuSecondaryItem(menu, item);
}

CS2M_API void CS2M_CALL cs2m_set_menu_empty(cs2m_handle menu, const char *title, const char *text, int loading)
{
	API()->SetMenuEmpty(menu, title ? title : "", text ? text : "", loading != 0);
}

CS2M_API int CS2M_CALL cs2m_begin_input(int slot, const char *prompt, const char *hint, cs2m_input_cancel_cb on_cancel, void *user)
{
	MenuInputCancelFn fn;
	if (on_cancel)
	{
		fn = [on_cancel, user](MenuHandle m, int s) { on_cancel(m, s, user); };
	}
	return API()->BeginMenuInput(slot, prompt ? prompt : "", hint ? hint : "", std::move(fn)) ? 1 : 0;
}

CS2M_API void CS2M_CALL cs2m_end_input(int slot)
{
	API()->EndMenuInput(slot);
}

CS2M_API void CS2M_CALL cs2m_set_input_clear_callback(cs2m_handle menu, cs2m_input_clear_cb on_clear, void *user)
{
	MenuInputClearFn fn;
	if (on_clear)
	{
		fn = [on_clear, user](MenuHandle m, int slot) { on_clear(m, slot, user); };
	}
	API()->SetMenuInputClearCallback(menu, std::move(fn));
}

// --- The display ---

CS2M_API int CS2M_CALL cs2m_show_message(int slot, const char *text, int tone, float seconds)
{
	return API()->ShowMenuMessage(slot, text ? text : "", static_cast<MenuTone>(tone), seconds) ? 1 : 0;
}

CS2M_API int CS2M_CALL cs2m_show_confirm(int slot, const char *title, const char *body, const char *cancel, const char *confirm, int danger,
										 cs2m_confirm_cb on_done, void *user)
{
	MenuConfirmFn fn;
	if (on_done)
	{
		fn = [on_done, user](int s, bool confirmed) { on_done(s, confirmed ? 1 : 0, user); };
	}
	return API()->ShowMenuConfirm(slot, title ? title : "", body ? body : "", cancel ? cancel : "", confirm ? confirm : "", danger != 0,
								  std::move(fn))
			   ? 1
			   : 0;
}

CS2M_API int CS2M_CALL cs2m_add_hint(int slot, const char *keys, const char *text)
{
	return API()->AddMenuHint(slot, keys ? keys : "", text ? text : "") ? 1 : 0;
}

CS2M_API void CS2M_CALL cs2m_clear_hint(int slot)
{
	API()->ClearMenuHint(slot);
}

CS2M_API void CS2M_CALL cs2m_hide_hint(int slot)
{
	API()->HideMenuHint(slot);
}

CS2M_API int CS2M_CALL cs2m_add_help(int slot, const char *keys, const char *text)
{
	return API()->AddMenuHelp(slot, keys ? keys : "", text ? text : "") ? 1 : 0;
}

CS2M_API void CS2M_CALL cs2m_clear_help(int slot)
{
	API()->ClearMenuHelp(slot);
}

CS2M_API void CS2M_CALL cs2m_set_mirrored(int slot, int mirrored)
{
	API()->SetMenuMirrored(slot, mirrored != 0);
}

CS2M_API void CS2M_CALL cs2m_set_text_features(cs2m_handle menu, int features)
{
	API()->SetMenuTextFeatures(menu, features);
}

CS2M_API int CS2M_CALL cs2m_get_text_features(cs2m_handle menu)
{
	return API()->GetMenuTextFeatures(menu);
}
