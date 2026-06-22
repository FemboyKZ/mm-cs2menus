#ifndef _INCLUDE_MENU_CONFIG_H_
#define _INCLUDE_MENU_CONFIG_H_

#include <string>

struct MenuGeneralCfg
{
	// Leading characters a player may type before a menu selection number, e.g.
	// "!1" or "/1" as well as bare "1".
	std::string commandPrefix = "!";       // normal (message would stay visible)
	std::string silentCommandPrefix = "/"; // silent (message suppressed)
};

struct MenuDefaultsCfg
{
	// Default render style applied when a consumer creates a menu with
	// MenuType::Default: "chat" or "html".
	std::string defaultType = "chat";
	// Items shown per page in a chat menu
	// (clamped to 1-7, the remaining keys 8/9/0 drive Next/Prev/Exit).
	int itemsPerPage = 7;
	// Default state of the "0. Exit" entry for newly created menus.
	bool exitButton = true;

	// --- HTML menus ---
	// Rows visible at once in an HTML menu (scrolling window, clamped 1-6).
	int htmlVisibleItems = 6;
	// Show a selectable "Exit" row in HTML menus.
	// Auto-forced on anyway when the Back key is disabled, so the menu is never left unexitable.
	bool htmlExitItem = false;
	// Hex colors for HTML markup.
	std::string htmlNavColor = "#ff2ee7";      // cursor row + marker
	std::string htmlFooterColor = "#909090";   // key-hint footer
	std::string htmlDisabledColor = "#808080"; // greyed-out items

	// Workaround for the center-HTML (show_survival_respawn_status) panel flashing:
	// fake CCSGameRules::m_bGameRestart while an HTML menu is shown.
	// NOTE: this breaks warmup UI while a menu is open, so it's off by default.
	bool htmlFixFlashing = false;

	// HTML navigation key bindings (by name). Valid names:
	//   w s a d, e/use, shift/speed, ctrl/duck, space/jump, r/reload,
	//   mouse1/attack, mouse2/attack2, tab. Unknown names keep the default.
	std::string navUp = "w";
	std::string navDown = "s";
	std::string navSelect = "d";
	std::string navBack = "a";
};

struct MenusConfig
{
	MenuGeneralCfg general;
	MenuDefaultsCfg menu;
};

// Parse cfg/cs2menus/core.cfg into `config`. Returns true on success,
// on failure `config` is left untouched (defaults remain in effect).
bool MENU_LoadConfig(const char *path, MenusConfig &config);

extern MenusConfig g_MenusConfig;

#endif // _INCLUDE_MENU_CONFIG_H_
