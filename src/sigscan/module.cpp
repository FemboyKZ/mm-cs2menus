#include "module.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#include <link.h>
#endif

namespace sig
{
#ifdef _WIN32
	bool GetModuleRange(const void *knownAddress, void *&outBase, size_t &outSize)
	{
		HMODULE hModule = nullptr;
		if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
								static_cast<LPCSTR>(knownAddress), &hModule)
			|| !hModule)
		{
			return false;
		}

		// HMODULE is the image base.
		// Read SizeOfImage straight from the PE headers so we don't need to link psapi.
		// The PE image is one contiguous mapping, so scanning the whole range is safe.
		auto *base = reinterpret_cast<uint8_t *>(hModule);
		auto *dos = reinterpret_cast<IMAGE_DOS_HEADER *>(base);
		if (dos->e_magic != IMAGE_DOS_SIGNATURE)
		{
			return false;
		}
		auto *nt = reinterpret_cast<IMAGE_NT_HEADERS *>(base + dos->e_lfanew);
		if (nt->Signature != IMAGE_NT_SIGNATURE)
		{
			return false;
		}

		outBase = base;
		outSize = nt->OptionalHeader.SizeOfImage;
		return true;
	}
#else
	bool GetModuleRange(const void *knownAddress, void *&outBase, size_t &outSize)
	{
		Dl_info dlInfo {};
		if (!dladdr(knownAddress, &dlInfo) || !dlInfo.dli_fbase)
		{
			return false;
		}

		auto *loadBase = static_cast<const uint8_t *>(dlInfo.dli_fbase);
		const ElfW(Ehdr) *ehdr = reinterpret_cast<const ElfW(Ehdr) *>(loadBase);
		const auto *phdr = reinterpret_cast<const ElfW(Phdr) *>(loadBase + ehdr->e_phoff);

		// Scan only the executable PT_LOAD segment (where the signature lives).
		// Scanning across the whole image would cross unmapped gaps and crash.
		for (int i = 0; i < ehdr->e_phnum; i++)
		{
			if (phdr[i].p_type == PT_LOAD && (phdr[i].p_flags & PF_X))
			{
				outBase = const_cast<uint8_t *>(loadBase + phdr[i].p_vaddr);
				outSize = phdr[i].p_memsz;
				return true;
			}
		}
		return false;
	}
#endif
} // namespace sig
