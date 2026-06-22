#ifndef _INCLUDE_MENU_SIGSCAN_MODULE_H_
#define _INCLUDE_MENU_SIGSCAN_MODULE_H_

#include <cstddef>
#include <cstdint>

namespace sig
{
	// Wildcard byte used in signatures.
	static constexpr uint8_t kWildcard = 0x2A;

	// Locate the loaded server module's mapped memory range by walking out from a
	// known pointer that lives inside it (e.g. g_pServerGameDLL).
	// Returns false if the range can't be determined. Implemented per-platform in module.cpp.
	bool GetModuleRange(const void *knownAddress, void *&outBase, size_t &outSize);

	// Scan [base, base+size] for the first occurrence of `sig` (kWildcard matches any byte).
	// Returns the match address, or nullptr if not found.
	inline void *FindSignature(void *base, size_t size, const uint8_t *signature, size_t sigLen)
	{
		if (!base || sigLen == 0 || size < sigLen)
		{
			return nullptr;
		}

		const uint8_t *mem = static_cast<const uint8_t *>(base);
		for (size_t i = 0; i + sigLen <= size; i++)
		{
			size_t matches = 0;
			while (matches < sigLen && (mem[i + matches] == signature[matches] || signature[matches] == kWildcard))
			{
				matches++;
			}
			if (matches == sigLen)
			{
				return const_cast<uint8_t *>(mem + i);
			}
		}
		return nullptr;
	}

	// Like FindSignature, but also reports whether a second match exists.
	// A non-unique signature means we can't trust the first hit,
	// so callers should refuse it rather than risk a bad pointer.
	inline void *FindSignatureUnique(void *base, size_t size, const uint8_t *signature, size_t sigLen, bool &outMultiple)
	{
		outMultiple = false;
		if (!base || sigLen == 0 || size < sigLen)
		{
			return nullptr;
		}

		const uint8_t *mem = static_cast<const uint8_t *>(base);
		void *first = nullptr;
		for (size_t i = 0; i + sigLen <= size; i++)
		{
			size_t matches = 0;
			while (matches < sigLen && (mem[i + matches] == signature[matches] || signature[matches] == kWildcard))
			{
				matches++;
			}
			if (matches == sigLen)
			{
				if (!first)
				{
					first = const_cast<uint8_t *>(mem + i);
				}
				else
				{
					outMultiple = true;
					return first;
				}
			}
		}
		return first;
	}

	// Resolve a RIP-relative MOV/LEA whose displacement begins 3 bytes into the
	// instruction (REX + opcode + ModRM), e.g. `48 8D 0D <disp32>`.
	// Returns the address the instruction computes: nextInsn + signed disp32.
	inline void *ResolveRipRelative(void *instruction)
	{
		if (!instruction)
		{
			return nullptr;
		}
		uint8_t *p = static_cast<uint8_t *>(instruction) + 3;
		int32_t disp = *reinterpret_cast<int32_t *>(p);
		p += 4; // advance to the next instruction (RIP base)
		return p + disp;
	}
} // namespace sig

#endif // _INCLUDE_MENU_SIGSCAN_MODULE_H_
