#ifndef _INCLUDE_MENU_KEY_TABLE_H_
#define _INCLUDE_MENU_KEY_TABLE_H_

#include "mmu/entity/in_buttons.h"
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
		{MenuButton::W, in_button::Forward, "W"},
		{MenuButton::S, in_button::Back, "S"},
		{MenuButton::A, in_button::MoveLeft, "A"},
		{MenuButton::D, in_button::MoveRight, "D"},
		{MenuButton::Use, in_button::Use, "E"},
		{MenuButton::Speed, in_button::Speed, "SHIFT"},
		{MenuButton::Duck, in_button::Duck, "CTRL"},
		{MenuButton::Jump, in_button::Jump, "SPACE"},
		{MenuButton::Reload, in_button::Reload, "R"},
		{MenuButton::Attack, in_button::Attack, "MOUSE1"},
		{MenuButton::Attack2, in_button::Attack2, "MOUSE2"},
		{MenuButton::Score, in_button::Score, "TAB"},
		{MenuButton::Inspect, in_button::Inspect, "F"},
	};
	inline constexpr int kKeyCount = static_cast<int>(sizeof(kKeys) / sizeof(kKeys[0]));

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
