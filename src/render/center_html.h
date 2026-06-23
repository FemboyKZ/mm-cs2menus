#ifndef _INCLUDE_MENU_CENTER_HTML_H_
#define _INCLUDE_MENU_CENTER_HTML_H_

#include <string>

namespace center_html
{
	// Resolve IGameEventManager2 by signature (see gamedata.h).
	// Call once after the engine interfaces are available.
	// Returns false if the signature didn't match,
	// in which case HTML menus are unavailable but chat menus still work.
	bool Init();

	// True if the game event manager was resolved (HTML menus usable).
	bool Available();

	// Send center-screen HTML to one player via the show_survival_respawn_status event.
	// The message decays after `durationSecs`, so callers re-send while the  menu stays open.
	// `html` uses CS2 markup: <font color='#hex' class='fontSize-s|sm|m'>, <br>.
	void Send(int slot, const char *html, int durationSecs);

	// Escape &, <, >, ", ' so arbitrary item text can't break the menu markup.
	std::string Escape(const std::string &text);

	// Escape `text` and translate any embedded chat color codes (\x01-\x10) into <font color> spans,
	//  so a chat-styled menu renders the same colors in HTML.
	// `defaultColor` is the hex used before the first code, `sizeClass` the fontSize class.
	std::string ColorizeChat(const std::string &text, const char *defaultColor, const char *sizeClass);
} // namespace center_html

#endif // _INCLUDE_MENU_CENTER_HTML_H_
