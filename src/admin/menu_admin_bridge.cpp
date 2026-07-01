#include "menu_admin_bridge.h"
#include "src/common.h"

static ICS2Admin *s_pAdmin = nullptr;

void MENU_AdminBridge_Init()
{
	s_pAdmin = nullptr;

	if (!g_SMAPI)
	{
		return;
	}

	void *iface = g_SMAPI->MetaFactory(CS2ADMIN_INTERFACE, nullptr, nullptr);
	if (iface)
	{
		s_pAdmin = static_cast<ICS2Admin *>(iface);
		META_CONPRINTF("[CS2Menus] mm-cs2admin found - command overrides active.\n");
	}
}

void MENU_AdminBridge_Refresh()
{
	// Called when any sibling plugin loads or unloads.
	// Drop the cached pointer unconditionally and re-resolve.
	ICS2Admin *prev = s_pAdmin;
	s_pAdmin = nullptr;

	if (!g_SMAPI)
	{
		return;
	}

	void *iface = g_SMAPI->MetaFactory(CS2ADMIN_INTERFACE, nullptr, nullptr);
	if (iface)
	{
		s_pAdmin = static_cast<ICS2Admin *>(iface);
		if (!prev)
		{
			META_CONPRINTF("[CS2Menus] mm-cs2admin loaded - command overrides active.\n");
		}
	}
	else if (prev)
	{
		META_CONPRINTF("[CS2Menus] mm-cs2admin unloaded - command overrides inactive.\n");
	}
}

void MENU_AdminBridge_Shutdown()
{
	s_pAdmin = nullptr;
}

bool MENU_AdminBridge_CanUseCommand(int slot, const char *commandName, uint32_t defaultFlag)
{
	// Console always passes
	if (slot < 0)
	{
		return true;
	}

	// Admin plugin not loaded. Open commands (defaultFlag 0) stay open,
	// commands with a configured flag stay blocked (restrictive fallback).
	if (!s_pAdmin)
	{
		return defaultFlag == 0;
	}

	return s_pAdmin->CanUseCommand(slot, commandName, "cs2menus", defaultFlag);
}
