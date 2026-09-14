#ifndef _INCLUDE_MENU_ENTITY_CCSCUSTOMHUDLAYOUT_H_
#define _INCLUDE_MENU_ENTITY_CCSCUSTOMHUDLAYOUT_H_

#include "mmu/entity/cbaseentity.h"
#include "mmu/schema.h"

#include <tier1/utlstring.h>

// SCHEMA_FIELD_OFFSET_FN for plain-struct fields, which may sit at 0. -1 while unresolved.
#define HUD_MEMBER_OFFSET_FN(fieldName) \
	static int32_t fieldName##_Offset() \
	{ \
		static int32_t offset = -1; \
		if (offset < 0) \
		{ \
			offset = schema::FindOffset(m_className, m_classNameHash, #fieldName, FNV1a(#fieldName)); \
		} \
		return offset; \
	}

class HUDPanelHasClass_t
{
public:
	DECLARE_SCHEMA_CLASS(HUDPanelHasClass_t)

	HUD_MEMBER_OFFSET_FN(m_nPanelIdIndex)   // uint16
	HUD_MEMBER_OFFSET_FN(m_nClassNameIndex) // uint16
	HUD_MEMBER_OFFSET_FN(m_eClassStatus)    // uint32, 0 without the class, 1 with it
};

class HUDPanelDialogVariableString_t
{
public:
	DECLARE_SCHEMA_CLASS(HUDPanelDialogVariableString_t)

	HUD_MEMBER_OFFSET_FN(m_nPanelIdIndex)        // uint16
	HUD_MEMBER_OFFSET_FN(m_nDialogVariableIndex) // uint16
	HUD_MEMBER_OFFSET_FN(m_sValue)               // CUtlString
	HUD_MEMBER_OFFSET_FN(m_bIsSet)               // bool
};

// A layout's applied classes and variables, global or for one player.
class CCSCustomHudLayoutState
{
public:
	DECLARE_SCHEMA_CLASS(CCSCustomHudLayoutState)

	SCHEMA_FIELD_OFFSET_FN(m_bInputCaptureEnabled)
	SCHEMA_FIELD_OFFSET_FN(m_playerSlot)
	SCHEMA_COLLECTION(HUDPanelHasClass_t, m_vecHasClasses)
	SCHEMA_COLLECTION(HUDPanelDialogVariableString_t, m_vecDialogVariableStrings)

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

	SCHEMA_FIELD_OFFSET_FN(m_globalLayoutState)
	SCHEMA_COLLECTION(CCSCustomHudLayoutState, m_vecPlayerLayoutStates)
	SCHEMA_COLLECTION(CUtlString, m_vecPanelIds)
	SCHEMA_COLLECTION(CUtlString, m_vecClassNames)
	SCHEMA_COLLECTION(CUtlString, m_vecDialogVariableNames)

	CCSCustomHudLayoutState *GlobalState()
	{
		const int16_t offset = m_globalLayoutState_Offset();
		return offset > 0 ? reinterpret_cast<CCSCustomHudLayoutState *>(reinterpret_cast<uintptr_t>(this) + offset) : nullptr;
	}
};

#endif // _INCLUDE_MENU_ENTITY_CCSCUSTOMHUDLAYOUT_H_
