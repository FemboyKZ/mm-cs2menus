#ifndef _INCLUDE_MENU_ENTITY_CCSPLAYERPAWN_H_
#define _INCLUDE_MENU_ENTITY_CCSPLAYERPAWN_H_

#include "schema.h"
#include "cbaseentity.h"
#include "in_buttons.h"

#include <cstdint>

// CBasePlayerPawn : CBaseEntity
class CBasePlayerPawn : public CBaseEntity
{
public:
	DECLARE_SCHEMA_CLASS(CBasePlayerPawn)

	// Currently-held button bitmask (m_pButtonStates[0]), or 0 if unavailable.
	uint64_t GetHeldButtons()
	{
		static int16_t offMovement =
			schema::GetOffset("CBasePlayerPawn", FNV1a("CBasePlayerPawn"), "m_pMovementServices", FNV1a("m_pMovementServices"));
		static int16_t offButtons =
			schema::GetOffset("CPlayer_MovementServices", FNV1a("CPlayer_MovementServices"), "m_nButtons", FNV1a("m_nButtons"));
		static int16_t offStates = schema::GetOffset("CInButtonState", FNV1a("CInButtonState"), "m_pButtonStates", FNV1a("m_pButtonStates"));

		if (offMovement <= 0 || offButtons <= 0 || offStates <= 0)
		{
			return 0;
		}

		void *services = *reinterpret_cast<void **>(reinterpret_cast<uintptr_t>(this) + offMovement);
		if (!services)
		{
			return 0;
		}

		uintptr_t buttonState = reinterpret_cast<uintptr_t>(services) + offButtons;
		return *reinterpret_cast<uint64_t *>(buttonState + offStates);
	}
};

// CCSPlayerPawn : CBasePlayerPawn
class CCSPlayerPawn : public CBasePlayerPawn
{
public:
	DECLARE_SCHEMA_CLASS(CCSPlayerPawn)
};

#endif // _INCLUDE_MENU_ENTITY_CCSPLAYERPAWN_H_
