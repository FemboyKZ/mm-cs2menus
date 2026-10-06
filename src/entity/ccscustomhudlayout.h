#ifndef _INCLUDE_MENU_ENTITY_CCSCUSTOMHUDLAYOUT_H_
#define _INCLUDE_MENU_ENTITY_CCSCUSTOMHUDLAYOUT_H_

#include "sdk/entity/cbaseentity.h"
#include "sdk/schema.h"

// The layout's state is written directly (see panorama_hud.cpp),
// like cs2kz-metamod's sdk/entity/ccscustomhudlayout.h.
class CCSCustomHudLayoutState
{
public:
	DECLARE_SCHEMA_CLASS(CCSCustomHudLayoutState)

	SCHEMA_FIELD_OFFSET_FN(m_playerSlot)

	// Not an entity, so it notifies through its own NetworkStateChanged, the second virtual.
	void Notify(const NetworkStateChangedData &data)
	{
		using NetworkStateChangedFn = void (*)(CCSCustomHudLayoutState *, const NetworkStateChangedData &);
		(*reinterpret_cast<NetworkStateChangedFn **>(this))[1](this, data);
	}

	void MarkChanged()
	{
		Notify(NetworkStateChangedData(true));
	}
};

class CCSCustomHudLayout : public CBaseEntity
{
public:
	DECLARE_SCHEMA_CLASS(CCSCustomHudLayout)

	SCHEMA_COLLECTION(CCSCustomHudLayoutState, m_vecPlayerLayoutStates)
};

#endif // _INCLUDE_MENU_ENTITY_CCSCUSTOMHUDLAYOUT_H_
