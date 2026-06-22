#ifndef _INCLUDE_MENU_GAMEDATA_H_
#define _INCLUDE_MENU_GAMEDATA_H_

#include <cstddef>
#include <cstdint>

// Platform-specific gamedata for HTML menus.
//
// Values from gamedata of cs2kz-metamod / CS2Fixes:
//   https://github.com/KZGlobalTeam/cs2kz-metamod/blob/master/gamedata/cs2kz-core.games.txt
//   https://github.com/Source2ZE/CS2Fixes/blob/main/gamedata/cs2fixes.games.txt
namespace gamedata
{
	static constexpr uint8_t kSigWildcard = 0x2A;

	// Offset from IGameResourceService to the CGameEntitySystem* pointer.
	// gamedata key: "GameEntitySystem"
#ifdef _WIN32
	static constexpr int kGameEntitySystemOffset = 88;
#else
	static constexpr int kGameEntitySystemOffset = 80;
#endif

	// Signature whose LEA loads the global IGameEventManager2 instance address.
	// gamedata key: "GameEventManager". Resolved with ResolveMovOrLea (RIP-rel).
#ifdef _WIN32
	// 48 8D 0D ?? ?? ?? ?? 49 8B 04 38   (lea rcx, [rip+x]; mov rax,[r8+rdi])
	static constexpr uint8_t kGameEventManagerSig[] = {0x48, 0x8D, 0x0D, 0x2A, 0x2A, 0x2A, 0x2A, 0x49, 0x8B, 0x04, 0x38};
#else
	// 4C 8D 25 ?? ?? ?? ?? 31 DB 48 83 EC 08   (lea r12, [rip+x]; ...)
	static constexpr uint8_t kGameEventManagerSig[] = {0x4C, 0x8D, 0x25, 0x2A, 0x2A, 0x2A, 0x2A, 0x31, 0xDB, 0x48, 0x83, 0xEC, 0x08};
#endif
	static constexpr size_t kGameEventManagerSigLen = sizeof(kGameEventManagerSig);
} // namespace gamedata

#endif // _INCLUDE_MENU_GAMEDATA_H_
