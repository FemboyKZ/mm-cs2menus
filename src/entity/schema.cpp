#include "schema.h"
#include "src/common.h"

#include <map>

// Per class cache: fieldNameHash -> offset
static std::map<uint32_t, std::map<uint32_t, int16_t>> g_schemaCache;

// Returns false when the schema system isn't ready yet (scope/class missing),
// so a transient early miss isn't cached as a permanent negative.
static bool InitSchemaFieldsForClass(const char *className, uint32_t classKey)
{
	CSchemaSystemTypeScope *pScope = g_pSchemaSystem->FindTypeScopeForModule(
#ifdef _WIN32
		"server.dll"
#else
		"libserver.so"
#endif
	);

	if (!pScope)
	{
		META_CONPRINTF("[CS2Menus] Schema: Could not find server type scope\n");
		return false;
	}

	SchemaClassInfoData_t *pClassInfo = pScope->FindDeclaredClass(className).Get();
	if (!pClassInfo)
	{
		META_CONPRINTF("[CS2Menus] Schema: Could not find class '%s'\n", className);
		return false;
	}

	// Creating the entry (even with zero fields) marks this class as probed,
	// so a fully-resolved-but-field-absent lookup won't re-scan every call.
	auto &classMap = g_schemaCache[classKey];

	for (int i = 0; i < pClassInfo->m_nFieldCount; i++)
	{
		auto &field = pClassInfo->m_pFields[i];
		uint32_t hash = FNV1a(field.m_pszName);
		classMap[hash] = field.m_nSingleInheritanceOffset;
	}
	return true;
}

int16_t schema::GetOffset(const char *className, uint32_t classKey, const char *fieldName, uint32_t fieldKey)
{
	auto classIt = g_schemaCache.find(classKey);
	if (classIt == g_schemaCache.end())
	{
		InitSchemaFieldsForClass(className, classKey);
		classIt = g_schemaCache.find(classKey);
	}

	if (classIt != g_schemaCache.end())
	{
		auto fieldIt = classIt->second.find(fieldKey);
		if (fieldIt != classIt->second.end())
		{
			return fieldIt->second;
		}
	}

	META_CONPRINTF("[CS2Menus] Schema: Could not find offset for %s::%s\n", className, fieldName);
	return 0;
}
