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

// Accessors guard against a 0 (failed) schema offset.
class CCSGameRules : public CGameRules
{
public:
	DECLARE_SCHEMA_CLASS(CCSGameRules)

	// Reads m_flRestartRoundTime. Returns false if the offset couldn't resolve.
	bool GetRestartRoundTime(float &out)
	{
		static int16_t off = schema::GetOffset("CCSGameRules", FNV1a("CCSGameRules"), "m_flRestartRoundTime", FNV1a("m_flRestartRoundTime"));
		if (off <= 0)
		{
			return false;
		}
		out = reinterpret_cast<GameTime_t *>(reinterpret_cast<uintptr_t>(this) + off)->GetTime();
		return true;
	}

	// Writes m_bGameRestart. Returns false if the offset couldn't resolve.
	bool SetGameRestart(bool value)
	{
		static int16_t off = schema::GetOffset("CCSGameRules", FNV1a("CCSGameRules"), "m_bGameRestart", FNV1a("m_bGameRestart"));
		if (off <= 0)
		{
			return false;
		}
		*reinterpret_cast<bool *>(reinterpret_cast<uintptr_t>(this) + off) = value;
		return true;
	}
};

// The networked entity (classname "cs_gamerules") that owns the CCSGameRules object.
class CCSGameRulesProxy : public CBaseEntity
{
public:
	DECLARE_SCHEMA_CLASS(CCSGameRulesProxy)

	CCSGameRules *GetGameRules()
	{
		static int16_t off = schema::GetOffset("CCSGameRulesProxy", FNV1a("CCSGameRulesProxy"), "m_pGameRules", FNV1a("m_pGameRules"));
		if (off <= 0)
		{
			return nullptr;
		}
		return *reinterpret_cast<CCSGameRules **>(reinterpret_cast<uintptr_t>(this) + off);
	}
};

#endif // _INCLUDE_MENU_ENTITY_CGAMERULES_H_
