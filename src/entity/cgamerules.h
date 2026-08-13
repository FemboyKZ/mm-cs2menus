#ifndef _INCLUDE_MENU_ENTITY_CGAMERULES_H_
#define _INCLUDE_MENU_ENTITY_CGAMERULES_H_

#include "mmu/schema.h"
#include "mmu/entity/cbaseentity.h"
#include <entity2/entitysystem.h> // GameTime_t

// Minimal gamerules wrappers for the HUD-flashing workaround.
// Only the fields we poke are declared.
// Layout/field names from CS2Fixes (src/cs2_sdk/entity/cgamerules.h).
class CGameRules
{
public:
	DECLARE_SCHEMA_CLASS(CGameRules)
};

class CCSGameRules : public CGameRules
{
public:
	DECLARE_SCHEMA_CLASS(CCSGameRules)

	SCHEMA_FIELD(GameTime_t, m_flRestartRoundTime)

	// Offset only. A reader would invite treating this write-only fake as truth.
	SCHEMA_FIELD_OFFSET_FN(m_bGameRestart)

	// Reads m_flRestartRoundTime. Returns false if the offset couldn't resolve.
	bool GetRestartRoundTime(float &out)
	{
		if (m_flRestartRoundTime_Offset() <= 0)
		{
			return false;
		}
		out = m_flRestartRoundTime().GetTime();
		return true;
	}

	// Writes m_bGameRestart. Returns false if the offset couldn't resolve.
	// Deliberately not SCHEMA_FIELD_NETWORKED: CCSGameRules is not a CEntityInstance,
	// so there is no NetworkStateChanged to call, and this fake is server-side only.
	bool SetGameRestart(bool value)
	{
		const int16_t offset = m_bGameRestart_Offset();
		if (offset <= 0)
		{
			return false;
		}
		*reinterpret_cast<bool *>(reinterpret_cast<uintptr_t>(this) + offset) = value;
		return true;
	}
};

// The networked entity (classname "cs_gamerules") that owns the CCSGameRules object.
class CCSGameRulesProxy : public CBaseEntity
{
public:
	DECLARE_SCHEMA_CLASS(CCSGameRulesProxy)

	SCHEMA_FIELD(CCSGameRules *, m_pGameRules)

	CCSGameRules *GetGameRules()
	{
		return m_pGameRules();
	}
};

#endif // _INCLUDE_MENU_ENTITY_CGAMERULES_H_
