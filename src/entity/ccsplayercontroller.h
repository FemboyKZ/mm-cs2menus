#ifndef _INCLUDE_MENU_ENTITY_CCSPLAYERCONTROLLER_H_
#define _INCLUDE_MENU_ENTITY_CCSPLAYERCONTROLLER_H_

#include "schema.h"
#include "cbaseentity.h"
#include "ccsplayerpawn.h"
#include <entity2/entitysystem.h>
#include <entity2/entityidentity.h>
#include <entityhandle.h>

// Resolve a CEntityHandle to a CEntityInstance via the entity system.
inline CEntityInstance *ResolveEntityHandle(const CEntityHandle &handle)
{
	extern CGameEntitySystem *g_pEntitySystem;
	if (!handle.IsValid() || !g_pEntitySystem)
	{
		return nullptr;
	}

	int entIndex = handle.GetEntryIndex();
	int chunk = entIndex / MAX_ENTITIES_IN_LIST;
	int offset = entIndex % MAX_ENTITIES_IN_LIST;

	if (chunk < 0 || chunk >= MAX_ENTITY_LISTS)
	{
		return nullptr;
	}

	CEntityIdentity *pChunk = g_pEntitySystem->m_EntityList.m_pIdentityChunks[chunk];
	if (!pChunk)
	{
		return nullptr;
	}

	CEntityIdentity *pIdent = &pChunk[offset];
	if (!pIdent || !pIdent->m_pInstance)
	{
		return nullptr;
	}

	return pIdent->m_pInstance;
}

// CBasePlayerController : CBaseEntity (m_hPawn)
class CBasePlayerController : public CBaseEntity
{
public:
	DECLARE_SCHEMA_CLASS(CBasePlayerController)

	SCHEMA_FIELD(CEntityHandle, m_hPawn)
};

// CCSPlayerController : CBasePlayerController
class CCSPlayerController : public CBasePlayerController
{
public:
	DECLARE_SCHEMA_CLASS(CCSPlayerController)

	SCHEMA_FIELD(CEntityHandle, m_hObserverPawn)

	CCSPlayerPawn *GetPawn()
	{
		return reinterpret_cast<CCSPlayerPawn *>(ResolveEntityHandle(m_hPawn()));
	}

	// Pawn to read button input from: the engine repoints m_hPawn to the controlled
	// pawn (player pawn when alive, observer pawn when dead/spectating), so prefer it.
	// Fall back to the observer pawn handle if m_hPawn is momentarily unset.
	// Returned as CBasePlayerPawn* since only GetHeldButtons (a base method) is needed.
	CBasePlayerPawn *GetInputPawn()
	{
		if (CEntityInstance *current = ResolveEntityHandle(m_hPawn()))
		{
			return reinterpret_cast<CBasePlayerPawn *>(current);
		}
		return reinterpret_cast<CBasePlayerPawn *>(ResolveEntityHandle(m_hObserverPawn()));
	}

	// Get controller from player slot (slot 0 -> entity index 1).
	static CCSPlayerController *FromSlot(int slot)
	{
		extern CGameEntitySystem *g_pEntitySystem;
		if (!g_pEntitySystem)
		{
			return nullptr;
		}

		int entIndex = slot + 1;
		int chunk = entIndex / MAX_ENTITIES_IN_LIST;
		int offset = entIndex % MAX_ENTITIES_IN_LIST;

		if (chunk < 0 || chunk >= MAX_ENTITY_LISTS)
		{
			return nullptr;
		}

		CEntityIdentity *pChunk = g_pEntitySystem->m_EntityList.m_pIdentityChunks[chunk];
		if (!pChunk)
		{
			return nullptr;
		}

		CEntityIdentity *pIdent = &pChunk[offset];
		if (!pIdent || !pIdent->m_pInstance)
		{
			return nullptr;
		}

		return reinterpret_cast<CCSPlayerController *>(pIdent->m_pInstance);
	}
};

#endif // _INCLUDE_MENU_ENTITY_CCSPLAYERCONTROLLER_H_
