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

	// Fallback language key for built-in label translations, used when a client's
	// cl_language is unknown or a phrase file lacks it. See translations/.
	std::string defaultLanguage = "en";

	// Mirror log output to addons/cs2menus/logs.
	bool logToFile = true;
	// Delete log files older than this many days, 0 keeps all.
	int logRetentionDays = 30;

	// Default state of the "0. Exit" entry for newly created menus.
	bool exitButton = true;

	// --- CHAT menus ---

	// Items shown per page in a chat menu
	// (clamped to 1-7, the remaining keys 8/9/0 drive Next/Prev/Exit).
	int itemsPerPage = 7;

	// Chat line colors, by name (the fixed client palette, not hex).
	// Names match the CHAT_COLOR_* set in common.h: default, darkred, purple, green, olive, lime, red,
	// grey, yellow, bluegrey, blue, darkblue, grey2, orchid, lightred, gold.
	// Unknown names keep the default.
	std::string chatTitleColor = "orchid";   // title text + its decoration
	std::string chatPageColor = "default";   // the "(page x/y)" indicator
	std::string chatItemColor = "default";   // enabled item number + text
	std::string chatDisabledColor = "grey";  // disabled item line
	std::string chatArrowColor = "orchid";   // Next/Prev/Exit arrow + label
	std::string chatHeaderColor = "default"; // optional header line (see ChatHeader)

	// Chat decoration templates ({placeholders} filled per row). Edit one part, keep the rest.
	std::string chatTitleFormat = "-- {title} --";       // {title} = title text (+ page indicator)
	std::string chatNumberFormat = "#{n} ";              // {n} = item / nav selection number
	std::string chatDisabledFormat = "#{n} ";            // {n} = number, used for disabled rows
	std::string chatArrow = "-> ";                       // before Next/Prev/Exit labels
	std::string chatPageFormat = "(page {cur}/{total})"; // {cur}/{total} = current/total page
	bool chatShowPage = true;                            // show the page indicator on multi-page menus
	// Optional branded line printed above the title (may use embedded color codes). Empty = none.
	std::string chatHeader;

	// --- HTML menus ---

	// Rows visible at once in an HTML menu (scrolling window, clamped 1-20).
	int htmlVisibleItems = 7;
	// Show a selectable "Exit" row in HTML menus.
	// Auto-forced on anyway when the Back key is disabled, so the menu is never left unexitable.
	bool htmlExitItem = false;
	// Hex colors for HTML markup.
	std::string htmlNavColor = "#ff2ee7";      // cursor row + marker
	std::string htmlFooterColor = "#909090";   // key-hint footer
	std::string htmlDisabledColor = "#808080"; // greyed-out items
	std::string htmlTitleColor = "#ff00e1";    // title accent
	std::string htmlItemColor = "#FFFFFF";     // normal item text
	// Size tokens (xs/s/sm/m/ml/l/xl/xxl/xxxl) and line alignment (left/center/right).
	std::string htmlTitleSize = "m";
	std::string htmlItemSize = "sm";
	std::string htmlAlign = "center";
	// Panorama classes applied to every line, space-separated. Empty = game default font.
	// Faces: stratum-{thin,light,regular,medium,bold,black}, add -italic or -condensed.
	// Monospace: stratum-{light,regular,bold}-mono, mono-spaced-font, mono-spaced-font-bold.
	// Effects stack: text-uppercase, text-letterspace-2px, text-shadow-basic.
	// Example: "stratum-bold text-uppercase text-shadow-basic"
	// Plus the cursor marker text.
	std::string htmlFontFace;
	std::string htmlMarker = "\xE2\x96\xB6 "; // ▶
	// Position counter color + size, size of the key-hint footer, and whether each is shown.
	std::string htmlCounterColor = "#9aa0a6";
	std::string htmlCounterSize = "s";
	std::string htmlFooterSize = "s";
	bool htmlShowCounter = true;
	bool htmlShowFooter = true;
	// Submenu-item suffix, footer segment separator, and whether the cursor row's text is recolored.
	std::string htmlSubmenuSuffix = " >";
	std::string htmlFooterSeparator = " | ";
	// Render templates ({placeholders} filled per row). Edit one part, keep the rest.
	std::string htmlCounterFormat = "[{cur}/{total}]";    // {cur}/{total} = position counter numbers
	std::string htmlFooterHintFormat = "{label}: {keys}"; // {label} = hint label, {keys} = key or key range
	std::string htmlFooterRangeFormat = "{up}/{down}";    // {up}/{down} = the two keys in the Move hint
	bool htmlHighlightText = true;
	// Center-panel resend cadence (the message decays, re-sent while open).
	// KeepAlive must stay below DurationSecs or the panel can blink.
	int htmlDurationSecs = 3;
	float htmlRefreshInterval = 1.0f;
	float htmlKeepAlive = 2.0f;

	// Workaround for the center-HTML (show_survival_respawn_status) panel flashing:
	// fake CCSGameRules::m_bGameRestart while an HTML menu is shown.
	// NOTE: this breaks warmup UI while a menu is open, so it's off by default.
	bool htmlFixFlashing = false;

	// HTML navigation key bindings (by name). Valid names:
	//   w s a d, e/use, shift/speed, ctrl/duck, space/jump, r/reload,
	//   mouse1/attack, mouse2/attack2, tab/score, f/inspect. Unknown names keep the default.
	std::string navUp = "w";
	std::string navDown = "s";
	std::string navSelect = "d";
	std::string navBack = "a";
};

// Optional sql_mm-backed store for per-player menu preferences (type + HTML nav keys).
// Off by default. When disabled, cs2menus uses only the server config (no per-player overrides).
struct MenuDatabaseCfg
{
	bool enabled = false;
	// "sqlite" or "mysql".
	std::string type = "sqlite";
	// Table-name prefix (the prefs table is "<prefix>_prefs").
	std::string prefix = "cs2menus";
	// SQLite: path relative to the game dir (e.g. game/csgo/).
	std::string path = "addons/cs2menus/cs2menus.db";
	// MySQL connection.
	std::string host = "localhost";
	std::string user = "root";
	std::string pass;
	std::string name = "cs2menus";
	int port = 3306;
};

struct MenusConfig
{
	MenuGeneralCfg general;
	MenuDefaultsCfg menu;
	MenuDatabaseCfg database;
};

// Parse cfg/cs2menus/core.cfg into `config`. Returns true on success,
// on failure `config` is left untouched (defaults remain in effect).
bool MENU_LoadConfig(const char *path, MenusConfig &config);

extern MenusConfig g_MenusConfig;

#endif // _INCLUDE_MENU_CONFIG_H_
