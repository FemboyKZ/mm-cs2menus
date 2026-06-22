#ifndef _INCLUDE_MENU_IN_BUTTONS_H_
#define _INCLUDE_MENU_IN_BUTTONS_H_

#include <cstdint>

// Player button bitmask values (m_pButtonStates[0]). From CS2Fixes globaltypes.h.
namespace in_button
{
	static constexpr uint64_t Attack = 0x1;        // mouse1
	static constexpr uint64_t Jump = 0x2;          // space
	static constexpr uint64_t Duck = 0x4;          // ctrl
	static constexpr uint64_t Forward = 0x8;       // W
	static constexpr uint64_t Back = 0x10;         // S
	static constexpr uint64_t Use = 0x20;          // E
	static constexpr uint64_t MoveLeft = 0x200;    // A
	static constexpr uint64_t MoveRight = 0x400;   // D
	static constexpr uint64_t Attack2 = 0x800;     // mouse2
	static constexpr uint64_t Reload = 0x2000;     // R
	static constexpr uint64_t Speed = 0x10000;     // shift (walk)
	static constexpr uint64_t Score = 0x200000000; // tab
} // namespace in_button

#endif // _INCLUDE_MENU_IN_BUTTONS_H_
