#ifndef _INCLUDE_MENU_ENTITY_CCSCUSTOMHUDLAYOUT_H_
#define _INCLUDE_MENU_ENTITY_CCSCUSTOMHUDLAYOUT_H_

#include "mmu/entity/cbaseentity.h"
#include "mmu/schema.h"

// Classes and dialog variables go through the game's own setters (see panorama_hud.cpp),
// only the per-player slot stamp is written here.
class CCSCustomHudLayoutState
{
public:
	DECLARE_SCHEMA_CLASS(CCSCustomHudLayoutState)

	SCHEMA_FIELD_OFFSET_FN(m_playerSlot)

	// Not an entity, so it notifies through its own NetworkStateChanged, the second virtual.
	void MarkChanged()
	{
		using NetworkStateChangedFn = void (*)(CCSCustomHudLayoutState *, const NetworkStateChangedData &);
		NetworkStateChangedData data(true);
		(*reinterpret_cast<NetworkStateChangedFn **>(this))[1](this, data);
	}
};

class CCSCustomHudLayout : public CBaseEntity
{
public:
	DECLARE_SCHEMA_CLASS(CCSCustomHudLayout)

	SCHEMA_COLLECTION(CCSCustomHudLayoutState, m_vecPlayerLayoutStates)
};

#endif // _INCLUDE_MENU_ENTITY_CCSCUSTOMHUDLAYOUT_H_
