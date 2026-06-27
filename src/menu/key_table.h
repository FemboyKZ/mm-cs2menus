#ifndef _INCLUDE_MENU_KEY_TABLE_H_
#define _INCLUDE_MENU_KEY_TABLE_H_

#include "src/entity/in_buttons.h"
#include "src/public/ics2menus.h"

#include <cstdint>
#include <string>

namespace keys
{
	struct KeyDef
	{
		MenuButton button;     // public enum value (Default/None are handled separately)
		uint64_t mask;         // IN_* button bit
		const char *canonical; // short name stored in configs/DB ("shift")
		const char *label;     // footer hint ("SHIFT")
		const char *aliases;   // space-separated alternates also accepted on input ("speed walk")
	};

	inline const KeyDef kKeys[] = {
		{MenuButton::W, in_button::Forward, "w", "W", "forward"},
		{MenuButton::S, in_button::Back, "s", "S", "back"},
		{MenuButton::A, in_button::MoveLeft, "a", "A", "left moveleft"},
		{MenuButton::D, in_button::MoveRight, "d", "D", "right moveright"},
		{MenuButton::Use, in_button::Use, "e", "E", "use interact"},
		{MenuButton::Speed, in_button::Speed, "shift", "SHIFT", "speed walk"},
		{MenuButton::Duck, in_button::Duck, "ctrl", "CTRL", "duck crouch"},
		{MenuButton::Jump, in_button::Jump, "space", "SPACE", "jump"},
		{MenuButton::Reload, in_button::Reload, "r", "R", "reload"},
		{MenuButton::Attack, in_button::Attack, "mouse1", "MOUSE1", "attack"},
		{MenuButton::Attack2, in_button::Attack2, "mouse2", "MOUSE2", "attack2"},
		{MenuButton::Score, in_button::Score, "tab", "TAB", "score"},
		{MenuButton::Inspect, in_button::Inspect, "f", "F", "inspect lookatweapon"},
	};
	inline constexpr int kKeyCount = static_cast<int>(sizeof(kKeys) / sizeof(kKeys[0]));

	// True if `name` is a whitespace-delimited token of `list`.
	inline bool ContainsWord(const char *list, const std::string &name)
	{
		std::string tok;
		for (const char *p = list;; p++)
		{
			if (*p == ' ' || *p == '\0')
			{
				if (!tok.empty() && tok == name)
				{
					return true;
				}
				tok.clear();
				if (*p == '\0')
				{
					return false;
				}
			}
			else
			{
				tok += *p;
			}
		}
	}

	// Look up a key by any accepted name (canonical or alias). Expects a lowercase name.
	inline const KeyDef *FindByName(const std::string &name)
	{
		for (const KeyDef &k : kKeys)
		{
			if (name == k.canonical || ContainsWord(k.aliases, name))
			{
				return &k;
			}
		}
		return nullptr;
	}

	// Look up a key by its public MenuButton value.
	inline const KeyDef *FindByButton(MenuButton button)
	{
		for (const KeyDef &k : kKeys)
		{
			if (k.button == button)
			{
				return &k;
			}
		}
		return nullptr;
	}

	// Look up a key by its IN_* button mask.
	inline const KeyDef *FindByMask(uint64_t mask)
	{
		for (const KeyDef &k : kKeys)
		{
			if (k.mask == mask)
			{
				return &k;
			}
		}
		return nullptr;
	}
} // namespace keys

#endif // _INCLUDE_MENU_KEY_TABLE_H_
