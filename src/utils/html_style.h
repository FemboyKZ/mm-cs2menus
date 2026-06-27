#ifndef _INCLUDE_MENU_HTML_STYLE_H_
#define _INCLUDE_MENU_HTML_STYLE_H_

#include <string>

namespace html_style
{
	// The size tokens the HTML renderer understands, mapping to Panorama fontSize classes (px in csgostyles.css): 
	// xs 8, s 12, sm 16, m 18, ml 20, l 24, xl 32, xxl 40, xxxl 64.
	inline bool IsSizeToken(const std::string &t)
	{
		return t == "xs" || t == "s" || t == "sm" || t == "m" || t == "ml" || t == "l" || t == "xl" || t == "xxl" || t == "xxxl";
	}
} // namespace html_style

#endif // _INCLUDE_MENU_HTML_STYLE_H_
