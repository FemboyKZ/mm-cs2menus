#ifndef _INCLUDE_MENU_PANORAMA_HUD_H_
#define _INCLUDE_MENU_PANORAMA_HUD_H_

#include <cstdint>
#include <string>
#include <vector>

class CCheckTransmitInfo;

// Menus in our workshop addon's windows (workshop/panorama/layout/custom_game/cs2menus/),
// one custom_hud_layout per player and layout, hidden from everyone but its owner. At most one shown.
// Menus go in the global state since the per-player states follow whoever a client watches.
namespace panorama_hud
{
	// Own file and id prefix each, since the client matches ids by name across every custom HUD layout.
	enum class Layout
	{
		List,     // menu.xml, "cm_"
		Grid,     // grid.xml, "cg_"
		Showcase, // showcase.xml, "cx_"
		Count,
	};

	// Fixed by the layouts.
	// The game interns at most 1024 panel ids and dialog variable names per layout, and every row run is one of each.
	// The list's 30 rows of 27 runs use about 915, the popups about 50 more.
	constexpr int kItemSlots = 30;
	constexpr int kNavSlots = 20;
	// Grid tiles (6 x 4 at the smallest size) and section tabs.
	constexpr int kGridSlots = 24;
	constexpr int kGridTabs = 10;

	// Grid tile sizes, the grid's cg_tiles gets no class, "size-m", "size-l" or "size-xl".
	enum class TileSize
	{
		Small,  // 6 x 4
		Medium, // 4 x 3
		Large,  // 3 x 2
		Cards,  // 3 x 1
	};
	int TileSlots(TileSize size);
	// Showcase buttons (3 x 6) and section tabs.
	constexpr int kShowcaseSlots = 18;
	constexpr int kShowcaseTabs = 10;
	// Differently colored runs per row.
	// The layout only takes plain text, so each run is its own label with a palette class.
	// rtv's longest label is the current map in !nominate with 5+ courses in both modes, 27 runs.
	constexpr int kRowSegments = 27;
	// Popups are shared by every row, so their ids don't grow with rows.
	constexpr int kListSlots = 16;
	constexpr int kStepButtons = 4;

	// CS_UM_CustomHudClicked
	constexpr int kClickMessageId = 390;

	// Text is plain, colors are "#RRGGBB" snapped to the addon's palette.
	struct View
	{
		struct Segment
		{
			std::string text;
			std::string color;
		};

		enum class Control
		{
			None,
			Toggle,
			Stepper,
			Choice,
		};

		struct Row
		{
			std::vector<Segment> segments; // at most kRowSegments
			std::string value;             // right-aligned, in the first segment's color
			bool disabled = false;
			Control control = Control::None;
			bool on = false;   // Toggle
			std::string image; // grid only, like "ak47"
		};

		struct Nav
		{
			std::string label;
			bool selected = false;
		};

		struct StepButton
		{
			std::string label; // like "-5", empty hides the button
			bool enabled = false;
		};

		// Beside the menu, at most one open.
		struct StepPopup
		{
			bool open = false;
			std::string title;
			std::string readout;
			StepButton buttons[kStepButtons];
		};

		struct ListPopup
		{
			bool open = false;
			std::string title;
			std::vector<Nav> rows; // at most kListSlots, selected marks the current option
			std::string page;      // like "2/3", empty hides the page arrows
			bool prev = false;
			bool next = false;
		};

		Layout layout = Layout::List;
		std::string title;
		std::string titleColor;
		std::string navColor;
		bool closeButton = true;
		// History buttons left of the close button. Back and forward are dimmed when unavailable, refresh is hidden.
		bool backButton = false;
		bool forwardButton = false;
		bool refreshButton = false;
		// Everything but the header hidden, from the collapse button left of close.
		bool collapsed = false;
		std::vector<Row> rows; // at most ItemSlots(layout): list rows or grid tiles
		TileSize tiles = TileSize::Small;
		std::vector<Nav> nav; // at most NavSlots(layout): the list's left column or the grid's tabs, empty hides them
		// Grid page arrows, empty page hides them.
		std::string page;
		bool prev = false;
		bool next = false;
		std::string fontClass; // from fonts.css, empty for the layout default
		// Large beside the menu box like the popups, which hide it while open. Showcase draws it inside. Grid image names.
		std::string image;
		// Showcase: the pinned item's button under the image, empty text hides it.
		std::string action;
		std::string actionColor;
		bool actionDisabled = false;
		bool sounds = true;
		StepPopup step;
		ListPopup list;
	};

	enum class Click
	{
		None,
		Close,
		Back,
		Forward,
		Refresh,
		Collapse,
		Action,     // the showcase's pinned item
		Nav,        // index = left-column slot or grid tab
		Item,       // index = row or tile on the page
		PopupClose, // either popup's close button
		Step,       // index = step button
		ListRow,    // index = list popup row
		ListPrev,
		ListNext,
		PagePrev, // grid page arrows
		PageNext,
	};

	int ItemSlots(Layout layout);
	int NavSlots(Layout layout);

	// Drops chat color codes.
	std::string StripColors(const std::string &text);

	// Splits chat-colored text into runs, starting in baseColor. Past kRowSegments the rest joins the last run.
	std::vector<View::Segment> SplitColors(const std::string &text, const std::string &baseColor);

	// No-op once the signatures resolve.
	bool Init();
	// Signatures resolved and the layout mounted.
	bool Available(Layout layout = Layout::List);

	// Also enables cursor mode and hides the player's other layout. False without a usable window.
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
