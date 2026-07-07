#ifndef _INCLUDE_ICS2ADMIN_H_
#define _INCLUDE_ICS2ADMIN_H_

#include <cstdint>

#define CS2ADMIN_INTERFACE "ICS2Admin002"

// Admin flag bits, mirrors SourceMod's admin flag letters a-z.
enum CS2AdminFlag : uint32_t
{
	CS2ADMIN_FLAG_NONE = 0,
	CS2ADMIN_FLAG_RESERVATION = (1 << 0), // a - Reserved slot
	CS2ADMIN_FLAG_GENERIC = (1 << 1),     // b - Generic admin
	CS2ADMIN_FLAG_KICK = (1 << 2),        // c - Kick
	CS2ADMIN_FLAG_BAN = (1 << 3),         // d - Ban
	CS2ADMIN_FLAG_UNBAN = (1 << 4),       // e - Unban
	CS2ADMIN_FLAG_SLAY = (1 << 5),        // f - Slay
	CS2ADMIN_FLAG_CHANGEMAP = (1 << 6),   // g - Map change
	CS2ADMIN_FLAG_CONVARS = (1 << 7),     // h - ConVar access
	CS2ADMIN_FLAG_CONFIG = (1 << 8),      // i - Config
	CS2ADMIN_FLAG_CHAT = (1 << 9),        // j - Chat commands
	CS2ADMIN_FLAG_VOTE = (1 << 10),       // k - Vote
	CS2ADMIN_FLAG_PASSWORD = (1 << 11),   // l - Password
	CS2ADMIN_FLAG_RCON = (1 << 12),       // m - RCON
	CS2ADMIN_FLAG_CHEATS = (1 << 13),     // n - Cheats
	CS2ADMIN_FLAG_CUSTOM1 = (1 << 14),    // o - Custom 1
	CS2ADMIN_FLAG_CUSTOM2 = (1 << 15),    // p - Custom 2
	CS2ADMIN_FLAG_CUSTOM3 = (1 << 16),    // q - Custom 3
	CS2ADMIN_FLAG_CUSTOM4 = (1 << 17),    // r - Custom 4
	CS2ADMIN_FLAG_CUSTOM5 = (1 << 18),    // s - Custom 5
	CS2ADMIN_FLAG_CUSTOM6 = (1 << 19),    // t - Custom 6
	CS2ADMIN_FLAG_ROOT = (1 << 25),       // z - Root (all access)
};

// Public admin interface for CS2Admin.
// Provides read-only access to admin status and permission checks,
// player state queries, and programmatic admin actions.
class ICS2Admin
{
public:
	// Admin queries

	// Check if a player in the given slot is an admin.
	// Returns true if the player has any admin flags assigned.
	virtual bool IsAdmin(int slot) = 0;

	// Get the raw admin flag bitmask for a player.
	// Returns 0 if the player is not an admin or the slot is invalid.
	virtual uint32_t GetAdminFlags(int slot) = 0;

	// Check if a player has a specific admin flag.
	// Root flag (z) always passes any single-flag check.
	virtual bool HasFlag(int slot, uint32_t flag) = 0;

	// Check if a player can use a specific command, considering
	// the full override chain (group overrides, global overrides,
	// then default flag).
	// commandName: the command without prefix, e.g. "ban".
	// commandGroup: optional group name, e.g. "admin". Can be nullptr.
	// defaultFlag: the default required flag if no override applies.
	virtual bool CanUseCommand(int slot, const char *commandName, const char *commandGroup, uint32_t defaultFlag) = 0;

	// Get the immunity level of a player.
	// Returns 0 if the player is not an admin or the slot is invalid.
	virtual int GetAdminImmunity(int slot) = 0;

	// Check if sourceAdmin has higher immunity than targetAdmin.
	// Useful for determining if one admin can act on another.
	virtual bool HasHigherImmunity(int sourceSlot, int targetSlot) = 0;

	// Player state queries

	// Check if a slot has a connected player.
	virtual bool IsPlayerConnected(int slot) = 0;

	// Get the player's in-game name. Returns nullptr if invalid slot.
	virtual const char *GetPlayerName(int slot) = 0;

	// Get the player's SteamID in STEAM_0:X:Y format. Returns nullptr if invalid.
	virtual const char *GetPlayerAuthId(int slot) = 0;

	// Get the player's 64-bit SteamID. Returns 0 if invalid.
	virtual uint64_t GetPlayerSteamID64(int slot) = 0;

	// Get the player's IP address. Returns nullptr if invalid.
	virtual const char *GetPlayerIP(int slot) = 0;

	// Check if a player is currently voice-muted.
	virtual bool IsMuted(int slot) = 0;

	// Check if a player is currently chat-gagged.
	virtual bool IsGagged(int slot) = 0;

	// Programmatic admin actions

	// Ban a player by slot. time = minutes (0 = permanent). adminSlot = -1 for console.
	virtual void BanPlayer(int targetSlot, int adminSlot, int timeMinutes, const char *reason) = 0;

	// Kick a player by slot. adminSlot = -1 for console.
	virtual void KickPlayer(int targetSlot, int adminSlot, const char *reason) = 0;

	// Mute a player (voice). time = minutes (0 = permanent). adminSlot = -1 for console.
	virtual void MutePlayer(int targetSlot, int adminSlot, int timeMinutes, const char *reason) = 0;

	// Gag a player (chat). time = minutes (0 = permanent). adminSlot = -1 for console.
	virtual void GagPlayer(int targetSlot, int adminSlot, int timeMinutes, const char *reason) = 0;

	// Silence a player (mute + gag). time = minutes (0 = permanent). adminSlot = -1 for console.
	virtual void SilencePlayer(int targetSlot, int adminSlot, int timeMinutes, const char *reason) = 0;

	// Remove voice mute from a player.
	virtual void UnmutePlayer(int targetSlot, int adminSlot) = 0;

	// Remove chat gag from a player.
	virtual void UngagPlayer(int targetSlot, int adminSlot) = 0;

	// Remove silence (both mute + gag) from a player.
	virtual void UnsilencePlayer(int targetSlot, int adminSlot) = 0;
};

#endif // _INCLUDE_ICS2ADMIN_H_
