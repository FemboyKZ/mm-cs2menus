#include "menu_admin_bridge.h"
#include "mmu/interface_bridge.h"
#include "mmu/log.h"
#include "src/common.h"

static mmu::InterfaceBridge<ICS2Admin> s_admin(CS2ADMIN_INTERFACE);

void MENU_AdminBridge_Init()
{
	if (s_admin.Refresh() == mmu::BridgeChange::Loaded)
	{
		MMU_LOG_INFO("mm-cs2admin found - command overrides active.\n");
	}
}

void MENU_AdminBridge_Refresh()
{
	switch (s_admin.Refresh())
	{
		case mmu::BridgeChange::Loaded:
			MMU_LOG_INFO("mm-cs2admin loaded - command overrides active.\n");
			break;
		case mmu::BridgeChange::Unloaded:
			MMU_LOG_INFO("mm-cs2admin unloaded - command overrides inactive.\n");
			break;
		case mmu::BridgeChange::Unchanged:
			break;
	}
}

void MENU_AdminBridge_Shutdown()
{
	s_admin.Shutdown();
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
	if (!s_admin)
	{
		return defaultFlag == 0;
	}

	return s_admin->CanUseCommand(slot, commandName, "cs2menus", defaultFlag);
}
