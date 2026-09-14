#ifndef _INCLUDE_MENU_PANORAMA_HUD_H_
#define _INCLUDE_MENU_PANORAMA_HUD_H_

#include <cstdint>
#include <string>
#include <vector>

class CCheckTransmitInfo;

// Menus in our workshop addon's window (workshop/panorama/layout/custom_game/cs2menus/menu.xml),
// one custom_hud_layout per player, hidden from everyone but its owner.
// Menus go in the global state since the per-player states follow whoever a client watches.
namespace panorama_hud
{
	// Fixed by the layout.
	constexpr int kItemSlots = 40;
	constexpr int kNavSlots = 20;

	// CS_UM_CustomHudClicked
	constexpr int kClickMessageId = 390;

	// Every text field is Panorama markup.
	struct View
	{
		struct Row
		{
			std::string text;
			std::string value; // right-aligned
			bool disabled = false;
		};

		struct Nav
		{
			std::string label;
			bool selected = false;
		};

		std::string title;
		bool closeButton = true;
		std::vector<Row> rows; // at most kItemSlots
		std::vector<Nav> nav;  // at most kNavSlots, empty hides the left column
		std::string fontClass; // from fonts.css, empty for the layout default
		bool sounds = true;
	};

	enum class Click
	{
		None,
		Close,
		Nav,  // index = left-column slot
		Item, // index = row on the page
	};

	// Drops chat color codes from text that isn't turned into markup.
	std::string StripColors(const std::string &text);

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

	// Multi-line status for the diagnostic command: signatures, layout, clicks and every live window.
	std::string Describe();

	void OnClientDisconnect(int slot);
	// The entities die with the map, only their handles are dropped.
	void OnLevelShutdown();
	// Removes windows a previous load left behind. Call on a late load.
	void RemoveOrphans();
	void Shutdown();
} // namespace panorama_hud

#endif // _INCLUDE_MENU_PANORAMA_HUD_H_
