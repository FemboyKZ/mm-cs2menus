#ifndef _INCLUDE_MENU_ENTITY_CBASEENTITY_H_
#define _INCLUDE_MENU_ENTITY_CBASEENTITY_H_

#include "schema.h"
#include <entity2/entityinstance.h>

// CBaseEntity : CEntityInstance
class CBaseEntity : public CEntityInstance
{
public:
	DECLARE_SCHEMA_CLASS(CBaseEntity)
};

#endif // _INCLUDE_MENU_ENTITY_CBASEENTITY_H_
