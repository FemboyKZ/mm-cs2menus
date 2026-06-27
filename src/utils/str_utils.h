#ifndef _INCLUDE_MENU_STR_UTILS_H_
#define _INCLUDE_MENU_STR_UTILS_H_

#include <algorithm>
#include <cctype>
#include <string>

namespace str
{
	// ASCII-lowercase a copy of `s`.
	inline std::string ToLower(const std::string &s)
	{
		std::string r = s;
		std::transform(r.begin(), r.end(), r.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		return r;
	}

	// ASCII-lowercase `s` in place.
	inline void ToLowerInPlace(std::string &s)
	{
		std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	}
} // namespace str

#endif // _INCLUDE_MENU_STR_UTILS_H_
