#ifndef _INCLUDE_MENU_PANORAMA_HUD_H_
#define _INCLUDE_MENU_PANORAMA_HUD_H_

#include <cstdint>
#include <string>
#include <vector>

class CCheckTransmitInfo;

// Menus in cs2kz's options window (panorama/layout/custom_game/cs2kz/menu.xml from its workshop addon),
// one custom_hud_layout per player, hidden from everyone but its owner.
// Menus go in the global state since the per-player states follow whoever a client watches.
namespace panorama_hud
{
	// Fixed by the layout.
	constexpr int kItemSlots = 20;
	constexpr int kNavSlots = 20;

	// CS_UM_CustomHudClicked
	constexpr int kClickMessageId = 390;

	struct View
	{
		struct Row
		{
			std::string text;
			bool disabled = false;
			bool submenu = false;
		};

		struct Nav
		{
			std::string label;
			bool selected = false;
		};

		std::string title;
		bool closeButton = true;
		std::vector<Row> rows; // at most kItemSlots
		std::vector<Nav> nav;  // at most kNavSlots, empty collapses the left column
	};

	enum class Click
	{
		None,
		Close,
		Nav,  // index = left-column slot
		Item, // index = row on the page
	};

	// No-op once the signatures resolve.
	bool Init();
	// Signatures resolved and the layout mounted.
	bool Available();

	// Also puts the player in cursor mode. False if the slot has no usable window.
	bool Show(int slot, const View &view);
	// Gives movement back. No-op if not shown.
	void Hide(int slot);
	bool IsShown(int slot);

	bool DecodeClick(const void *buf, uint32_t size, uint32_t &layoutHandle, std::string &buttonId);
	// None for a click on anyone else's window.
	Click ParseClick(int slot, uint32_t layoutHandle, const char *buttonId, int &index);

	void OnCheckTransmit(CCheckTransmitInfo **infos, int count);

	void OnClientDisconnect(int slot);
	// The entities die with the map, only their handles are dropped.
	void OnLevelShutdown();
	// Removes windows a previous load left behind. Call on a late load.
	void RemoveOrphans();
	void Shutdown();
} // namespace panorama_hud

#endif // _INCLUDE_MENU_PANORAMA_HUD_H_
