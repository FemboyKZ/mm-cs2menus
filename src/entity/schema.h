#ifndef _INCLUDE_MENU_SCHEMA_H_
#define _INCLUDE_MENU_SCHEMA_H_

#include <cstdint>
#include <schemasystem/schemasystem.h>

namespace schema
{
	int16_t GetOffset(const char *className, uint32_t classKey, const char *fieldName, uint32_t fieldKey);
}

// FNV1a hash at compile time
constexpr uint32_t FNV1a(const char *str)
{
	uint32_t hash = 0x811C9DC5u;
	while (*str)
	{
		hash ^= static_cast<uint32_t>(*str++);
		hash *= 0x01000193u;
	}
	return hash;
}

// Declares the schema class metadata for this wrapper.
// Must appear at the top of each entity wrapper class body.
#define DECLARE_SCHEMA_CLASS(className) \
	static constexpr const char *m_className = #className; \
	static constexpr uint32_t m_classNameHash = FNV1a(#className);

// Read only schema field accessor.
// Creates an inline method that returns a reference to the field at the cached offset.
#define SCHEMA_FIELD(type, fieldName) \
	type &fieldName() \
	{ \
		static constexpr uint32_t fieldHash = FNV1a(#fieldName); \
		static int16_t offset = schema::GetOffset(m_className, m_classNameHash, #fieldName, fieldHash); \
		return *reinterpret_cast<type *>(reinterpret_cast<uintptr_t>(this) + offset); \
	}

#endif // _INCLUDE_MENU_SCHEMA_H_
