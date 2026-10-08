#ifndef _INCLUDE_MENU_KEY_TABLE_H_
#define _INCLUDE_MENU_KEY_TABLE_H_

#include "sdk/entity/in_buttons.h"
#include "interfaces/cs2menus/ics2menus.h"

#include <cstdint>
#include <string>

// IN_* mask and footer label per key. Config names live in ics2menus.h (kMenuButtonNames).
// kKeys order is the prefs menu cycle order.
namespace keys
{
	struct KeyDef
	{
		MenuButton button; // public enum value (Default/None are handled separately)
		uint64_t mask;     // IN_* button bit
		const char *label; // footer hint ("SHIFT")
	};

	inline const KeyDef kKeys[] = {
		{MenuButton::W, IN_FORWARD, "W"},
		{MenuButton::S, IN_BACK, "S"},
		{MenuButton::A, IN_MOVELEFT, "A"},
		{MenuButton::D, IN_MOVERIGHT, "D"},
		{MenuButton::Use, IN_USE, "E"},
		{MenuButton::Speed, IN_SPEED, "SHIFT"},
		{MenuButton::Duck, IN_DUCK, "CTRL"},
		{MenuButton::Jump, IN_JUMP, "SPACE"},
		{MenuButton::Reload, IN_RELOAD, "R"},
		{MenuButton::Attack, IN_ATTACK, "MOUSE1"},
		{MenuButton::Attack2, IN_ATTACK2, "MOUSE2"},
		{MenuButton::Score, IN_SCORE, "TAB"},
		{MenuButton::Inspect, IN_LOOK_AT_WEAPON, "F"},
	};
	inline constexpr int kKeyCount = static_cast<int>(sizeof(kKeys) / sizeof(kKeys[0]));
	static_assert(kKeyCount == kMenuButtonNameCount, "every key needs an entry in kMenuButtonNames");

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

	// Look up a key by any accepted name (canonical or alias). Expects a lowercase name.
	// Null for "none", "off" and unknown names.
	inline const KeyDef *FindByName(const std::string &name)
	{
		return FindByButton(ParseMenuButton(name));
	}

	// Name written to configs and the prefs DB.
	inline const char *Canonical(const KeyDef &k)
	{
		return GetMenuButtonName(k.button);
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
