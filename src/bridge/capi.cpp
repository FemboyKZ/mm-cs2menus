// Flat C ABI over ICS2Menus002 for managed hosts (SwiftlyS2, CounterStrikeSharp).
// See src/public/cs2menus_capi.h for the contract.

#define CS2MENUS_EXPORTS
#include "src/public/cs2menus_capi.h"
#include "src/public/ics2menus.h"

#include <cstring>

// Defined in cs2menus.cpp: the singleton ICS2Menus002 implementation.
// Routing through it inherits the curtime stamping and off-thread queueing the interface wrapper already does.
extern ICS2Menus *Cs2Menus_GetLocalAPI();

namespace
{
	ICS2Menus *API()
	{
		return Cs2Menus_GetLocalAPI();
	}

	// Copy a (possibly null) C string into a caller buffer.
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

CS2M_API int CS2M_CALL cs2m_abi_version(void)
{
	return CS2M_ABI_VERSION;
}

CS2M_API int CS2M_CALL cs2m_available(void)
{
	return API() != nullptr ? 1 : 0;
}

CS2M_API cs2m_handle CS2M_CALL cs2m_create(int type, const char *title, cs2m_select_cb on_select, void *user)
{
	ICS2Menus *api = API();
	if (!api)
	{
		return kInvalidMenuHandle;
	}

	MenuItemSelectFn fn;
	if (on_select)
	{
		// Capture the raw pointer + token, not a managed object:
		// the host owns lifetime and must cs2m_destroy before its assembly unloads.
		fn = [on_select, user](MenuHandle menu, int slot, int item) { on_select(menu, slot, item, user); };
	}
	return api->CreateMenu(static_cast<MenuType>(type), title ? title : "", std::move(fn));
}

CS2M_API int CS2M_CALL cs2m_add_item(cs2m_handle menu, const char *text, const char *info, int disabled)
{
	return API()->AddItem(menu, text ? text : "", info ? info : "", disabled != 0);
}

CS2M_API void CS2M_CALL cs2m_set_title(cs2m_handle menu, const char *title)
{
	API()->SetTitle(menu, title ? title : "");
}

CS2M_API void CS2M_CALL cs2m_set_exit_button(cs2m_handle menu, int enabled)
{
	API()->SetExitButton(menu, enabled != 0);
}

CS2M_API void CS2M_CALL cs2m_set_close_on_select(cs2m_handle menu, int enabled)
{
	API()->SetCloseOnSelect(menu, enabled != 0);
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

CS2M_API void CS2M_CALL cs2m_set_exit_item(cs2m_handle menu, int enabled)
{
	API()->SetExitItem(menu, enabled != 0);
}

CS2M_API void CS2M_CALL cs2m_set_menu_key(cs2m_handle menu, int action, int button)
{
	API()->SetMenuKey(menu, static_cast<MenuNavAction>(action), static_cast<MenuButton>(button));
}

CS2M_API void CS2M_CALL cs2m_set_menu_label(cs2m_handle menu, int label, const char *text)
{
	API()->SetMenuLabel(menu, static_cast<MenuLabel>(label), text ? text : "");
}

CS2M_API int CS2M_CALL cs2m_get_menu_label(cs2m_handle menu, int label, char *buf, int buflen)
{
	// Copy immediately: the returned pointer aliases live menu storage.
	return CopyOut(API()->GetMenuLabel(menu, static_cast<MenuLabel>(label)), buf, buflen);
}

CS2M_API void CS2M_CALL cs2m_set_menu_style(cs2m_handle menu, int field, const char *value)
{
	API()->SetMenuStyle(menu, static_cast<MenuStyle>(field), value ? value : "");
}

CS2M_API int CS2M_CALL cs2m_get_menu_style(cs2m_handle menu, int field, char *buf, int buflen)
{
	// Copy immediately: the returned pointer aliases live menu storage.
	return CopyOut(API()->GetMenuStyle(menu, static_cast<MenuStyle>(field)), buf, buflen);
}

CS2M_API void CS2M_CALL cs2m_set_force_type(cs2m_handle menu, int force)
{
	API()->SetMenuForceType(menu, force != 0);
}

CS2M_API void CS2M_CALL cs2m_set_start_item(cs2m_handle menu, int item)
{
	API()->SetStartItem(menu, item);
}

CS2M_API int CS2M_CALL cs2m_add_submenu(cs2m_handle parent, const char *text, cs2m_handle child, const char *info)
{
	return API()->AddSubMenu(parent, text ? text : "", child, info ? info : "");
}

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

CS2M_API int CS2M_CALL cs2m_get_active_type(int slot)
{
	return static_cast<int>(API()->GetActiveMenuType(slot));
}

CS2M_API int CS2M_CALL cs2m_get_selected_item(int slot)
{
	return API()->GetSelectedItem(slot);
}

CS2M_API void CS2M_CALL cs2m_set_external_busy(int slot, int busy)
{
	API()->SetExternalBusy(slot, busy != 0);
}

CS2M_API int CS2M_CALL cs2m_get_external_busy(int slot)
{
	return API()->GetExternalBusy(slot) ? 1 : 0;
}

CS2M_API void CS2M_CALL cs2m_destroy(cs2m_handle menu)
{
	API()->DestroyMenu(menu);
}

CS2M_API int CS2M_CALL cs2m_item_count(cs2m_handle menu)
{
	return API()->GetItemCount(menu);
}

CS2M_API int CS2M_CALL cs2m_get_item_text(cs2m_handle menu, int item, char *buf, int buflen)
{
	// Copy immediately: the returned pointer aliases live menu storage.
	return CopyOut(API()->GetItemText(menu, item), buf, buflen);
}

CS2M_API int CS2M_CALL cs2m_get_item_info(cs2m_handle menu, int item, char *buf, int buflen)
{
	return CopyOut(API()->GetItemInfo(menu, item), buf, buflen);
}

CS2M_API void CS2M_CALL cs2m_set_item_text(cs2m_handle menu, int item, const char *text)
{
	API()->SetItemText(menu, item, text ? text : "");
}

CS2M_API void CS2M_CALL cs2m_set_item_info(cs2m_handle menu, int item, const char *info)
{
	API()->SetItemInfo(menu, item, info ? info : "");
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
	// Copy immediately: the returned pointer aliases live menu storage.
	return CopyOut(API()->GetItemIcon(menu, item), buf, buflen);
}

CS2M_API void CS2M_CALL cs2m_remove_item(cs2m_handle menu, int item)
{
	API()->RemoveItem(menu, item);
}

CS2M_API void CS2M_CALL cs2m_remove_all_items(cs2m_handle menu)
{
	API()->RemoveAllItems(menu);
}

CS2M_API int CS2M_CALL cs2m_get_start_item(cs2m_handle menu)
{
	return API()->GetStartItem(menu);
}
