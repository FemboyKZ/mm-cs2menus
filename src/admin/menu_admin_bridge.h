#ifndef _INCLUDE_MENU_ADMIN_BRIDGE_H_
#define _INCLUDE_MENU_ADMIN_BRIDGE_H_

// Optional integration with mm-cs2admin (ICS2Admin002 interface).
// Lets admin_overrides.cfg gate cs2menus commands.
// If mm-cs2admin is not loaded, commands with a default flag of 0 stay open
// and any command with a non-zero default flag is denied.

#include "vendor/interfaces/ics2admin.h"
#include <cstdint>

// Call once in AllPluginsLoaded() to try to acquire the ICS2Admin interface.
void MENU_AdminBridge_Init();

// Re-resolve the interface.
// Call from OnPluginLoad and OnPluginUnload so we never hold a dangling pointer into a freed plugin.
void MENU_AdminBridge_Refresh();

// Drop any cached interface pointer. Call from plugin Unload().
void MENU_AdminBridge_Shutdown();

// Consults the full override chain in mm-cs2admin (group overrides, admin_overrides.cfg, then defaultFlag).
// commandName lets admin_overrides.cfg target this command by name.
// defaultFlag 0 means the command is open to everyone unless an override gates it.
// slot < 0 = console = true.
// If the admin plugin is not loaded, open commands (defaultFlag 0) pass and gated ones are denied.
bool MENU_AdminBridge_CanUseCommand(int slot, const char *commandName, uint32_t defaultFlag);

#endif // _INCLUDE_MENU_ADMIN_BRIDGE_H_
