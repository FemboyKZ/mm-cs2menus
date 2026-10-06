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
		Studio,   // studio.xml, "cs_": the showcase's buttons at the right edge, the rest of the screen a 3D preview
		Columns,  // columns.xml, "cl_": a column per section, each scrolling on its own
		Table,    // table.xml, "ct_": a name and its cells per row under column headings
		Notice,   // notice.xml, "cn_". Last, see kMenuLayouts.
		Count,
	};

	// Fixed by the layouts.
	// The game interns at most 1024 panel ids and dialog variable names per layout, and every row run is one of each.
	// The list's 28 rows of 27 runs, a value and its two step buttons use about 870, the popups about 50 more.
	constexpr int kItemSlots = 28;
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
	// Showcase and studio buttons, and section tabs. A page holds 3 x 6 items: the slots past those are for the ones that
	// take more than one, a Choice drawn as a segment per option.
	constexpr int kShowcaseSlots = 24;
	constexpr int kShowcasePage = 18;
	constexpr int kShowcaseTabs = 10;
	// The most options a Choice is drawn in place with, a row of segments. More open the list popup.
	constexpr int kSegments = 5;
	// Studio control panel buttons (3 x 6), and its tabs, one per section of the control items.
	constexpr int kStudioControls = 18;
	constexpr int kStudioControlTabs = 4;
	// Columns, and tiles in each. A tile's slot is column * kColumnRows + its place in the column.
	constexpr int kColumns = 4;
	constexpr int kColumnRows = 16;
	constexpr int kColumnSlots = kColumns * kColumnRows;
	// The table's rows, the keys over them, the cells in a row and the column headings.
	constexpr int kTableRows = 18;
	constexpr int kTableKeys = 30;
	constexpr int kTableCells = 12;
	constexpr int kTableColumns = 6;
	// The info card's label and value pairs, and its bar's bands.
	constexpr int kInfoRows = 10;
	constexpr int kInfoBands = 5;
	// The studio's hint pill: parts, and key caps in each.
	constexpr int kHintParts = 5;
	constexpr int kHintKeys = 4;
	// The studio's key list: rows, and key caps in each.
	constexpr int kHelpRows = 8;
	constexpr int kHelpKeys = 3;
	// Chips under the tabs, at the tab row's end in the columns and the table. Every layout but the list.
	constexpr int kChipSlots = 6;
	// Showcase and studio pages of image tiles (3 x 4), out of the buttons.
	constexpr int kStudioImageSlots = 12;
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

		// Same order as MenuCorner.
		enum class Corner
		{
			None,
			Star,
			StarOn,
			StarUndo,
			Copy,
		};

		struct Row
		{
			std::vector<Segment> segments; // at most kRowSegments
			std::vector<Segment> cells;    // table: at most kTableCells
			bool details = false;          // table: the button at the row's end
			std::string value;             // right-aligned, in the first segment's color
			bool disabled = false;
			Control control = Control::None;
			bool on = false;       // Toggle
			std::string image;     // an image tile's picture, like "ak47". Not in the list.
			std::string imageTint; // a tint-<token> class on the image
			// Image tiles' badges: a rar-<rarity> class, the tag label with a tag-<token> class, team dots, a lock and the
			// corner button. A showcase or studio button without an image takes the tag and the rarity only.
			std::string rarity;
			std::string tag;
			std::string tagStyle; // a class-safe token, "" for the plain look
			int teams = 0;        // bit 1 T, bit 2 CT
			bool locked = false;
			Corner corner = Corner::None;
			// Showcase and studio buttons: a readout shows `value` as a small label over its text and isn't pressed,
			// a highlight takes the accent, a span is how many of the three columns it takes.
			bool readout = false;
			bool highlight = false;
			int span = 1;
			// One option of a Choice drawn in place: how many options it has and which this is. 0 for any other button.
			int segCount = 0;
			int segIndex = 0;
			// A label over what follows: a row of its own among buttons, a sub-heading in a column.
			bool heading = false;
			// Columns: the tile's slot, -1 when its column is full, and half a row wide.
			int slot = -1;
			bool half = false;
		};

		struct Nav
		{
			std::string label;
			bool selected = false;
			bool marked = false; // a dot on the tab
		};

		// Columns: one's heading and how many items are in it.
		struct Column
		{
			std::string label;
			int count = 0;
		};

		struct Chip
		{
			std::string label;
			std::string value; // the selected option beside the label, a note's text
			bool on = false;
			bool menu = false; // has options, a caret after it
			bool note = false; // says something, takes no click
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

		struct ListRow
		{
			std::string label;
			std::string sub;       // a second line, not in the list layout
			bool selected = false; // the current option
			bool marked = false;   // a dot before it
			int tone = 0;          // as MenuTone
		};

		struct ListPopup
		{
			bool open = false;
			std::string title;
			std::vector<ListRow> rows; // at most kListSlots
			std::string page;          // like "2/3", empty hides the page arrows
			bool prev = false;
			bool next = false;
		};

		Layout layout = Layout::List;
		std::string title;
		std::string titleColor;
		std::string navColor;
		bool closeButton = true;
		// History buttons left of the close button. Dimmed when unavailable.
		bool backButton = false;
		bool forwardButton = false;
		bool refreshButton = false;
		// Everything but the header hidden, from the collapse button left of close.
		bool collapsed = false;
		std::vector<Row> rows; // at most ItemSlots(layout): list rows or grid tiles
		TileSize tiles = TileSize::Small;
		std::vector<Nav> nav; // at most NavSlots(layout): the list's left column or the grid's tabs, empty hides them
		// The tab the ones without room hide behind, like "+3": its place among `nav`, -1 for none.
		int navMore = -1;
		std::vector<Chip> chips; // at most kChipSlots, empty hides the row

		// Table: the first is over the rows' text, each one after it over `cells` of their cells.
		struct Head
		{
			std::string label;
			int cells = 0;
			int sort = 0; // an arrow: 1 up, -1 down
		};

		std::vector<Head> heads; // at most kTableColumns
		bool headButtons = false;
		// Some row of the menu has details, so every row keeps the button's place.
		bool details = false;
		std::vector<Column> columns; // columns only, at most kColumns

		// The input field above the chips: a menu's Input item, or a prompt while waiting for chat.
		struct Input
		{
			bool shown = false;
			bool typing = false;
			bool overlay = false;     // no Input item, laid over the first row while typing
			bool placeholder = false; // nothing typed, the text is the item's subtext
			bool clear = false;       // the clear button at its end
			std::string text;
			std::string hint; // at the end: a range while typing, a result count after
		};

		Input input;
		// Under the window, empty hides it. Tone as MenuTone: 0 info, 1 ok, 2 warn, 3 bad.
		std::string message;
		int messageTone = 0;
		// In the item area when there are no rows, an empty title hides it. Loading pulses.
		std::string emptyTitle;
		std::string emptyText;
		bool emptyLoading = false;
		// The header's marker for unsaved changes, empty hides it.
		std::string edited;

		// Showcase and studio: the card for what's being edited. Percentages are whole, 0 to 100.
		struct Info
		{
			bool shown = false;
			std::string title;
			std::string subtitle;
			std::string subtitleColor; // "#RRGGBB", a rarity token or empty
			bool meter = false;
			int mark = 0;    // where the marker sits
			int rangeLo = 0; // dimmed outside these
			int rangeHi = 100;
			std::vector<int> bands; // each band's width, at most kInfoBands, empty for one plain bar
			std::string meterLabel;
			std::string meterValue;

			struct Row
			{
				std::string label;
				std::string value;
				bool wide = false; // takes the whole line
			};

			std::vector<Row> rows; // at most kInfoRows
		};

		Info info;

		// Studio: the hint pill, key caps and what they do. No keys is a caption.
		struct HintPart
		{
			std::vector<std::string> keys; // at most kHintKeys
			std::string text;
		};

		std::vector<HintPart> hint; // at most kHintParts, none hides the pill
		// Studio: the key list behind the "?" button at the control tabs' end, rows as the hint's parts, at most
		// kHelpRows. None hides the button. Open, it takes the info card's place under `helpTitle`.
		std::vector<HintPart> help;
		bool helpOpen = false;
		std::string helpTitle;
		// Studio: the box at the left edge, the control panel at the right.
		bool mirrored = false;
		// The header's scope chip, empty hides it. Teams bit 1 T, bit 2 CT, a button when it takes clicks.
		std::string scope;
		int scopeTeams = 0;
		bool scopeButton = false;

		struct Dialog
		{
			bool open = false;
			std::string title;
			std::string body;
			std::string cancel;
			std::string confirm;
			bool danger = false;
		};

		Dialog dialog;
		// The page arrows of every layout but the list, an empty page hides them.
		std::string page;
		bool prev = false;
		bool next = false;
		std::string fontClass; // from fonts.css, empty for the layout default
		// Large beside the menu box like the popups, which hide it while open. Showcase draws it inside. Grid image names.
		std::string image;
		// Showcase and studio: the pinned item's button under the buttons, empty text hides it.
		std::string action;
		std::string actionColor;
		bool actionDisabled = false;
		// The secondary item's small button before it, empty text hides it.
		std::string action2;
		bool action2Disabled = false;
		// Studio: the cursor off so the mouse turns the view, from a click on the preview.
		bool turning = false;

		// Studio: the control panel's buttons, at most kStudioControls. None hides it.
		struct ControlButton
		{
			std::string label;
			bool disabled = false;
			// As Row's.
			std::string sub;
			bool readout = false;
			bool highlight = false;
			int span = 1;
			int segCount = 0;
			int segIndex = 0;
			bool heading = false;
		};

		std::vector<ControlButton> controls;
		// At most kStudioControlTabs, fewer than 2 hides the row.
		std::vector<Nav> controlTabs;
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
		Action2,    // the secondary item beside it
		Stage,      // the studio's preview, anywhere off the box
		Control,    // index = studio control panel button
		ControlTab, // index = studio control panel tab
		Help,       // the studio's key list button
		Nav,        // index = left-column slot or grid tab
		Item,       // index = row or tile on the page
		ItemDec,    // index = list row, the button before its value
		ItemInc,    // the one after it
		Corner,     // index = tile on the page, its corner button
		Chip,       // index = filter chip
		Input,      // the input field
		InputClear, // its clear button
		Scope,      // the header's scope chip
		DialogYes,  // the confirm dialog's confirm button
		DialogNo,   // its cancel button, or anywhere off it
		PopupClose, // either popup's close button
		Step,       // index = step button
		ListRow,    // index = list popup row
		ListPrev,
		ListNext,
		PagePrev, // grid page arrows
		PageNext,
		Notice,
		Column, // index = table heading
	};

	// Plain text, an empty line is left out.
	struct Notice
	{
		std::string title;
		std::string time; // at the title's end
		std::string text;
		std::string hint;
		std::string fontClass;
	};

	int ItemSlots(Layout layout);
	int NavSlots(Layout layout);

	// Drops chat color codes.
	std::string StripColors(const std::string &text);

	// How wide a text runs, in characters: 2 for the wide ones (CJK, fullwidth), 0 for what takes no room.
	// The server can't measure text, this is what its guesses at what fits go by.
	int TextCells(const std::string &text);

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

	// Never enables cursor mode. False without a usable window.
	bool ShowNotice(int slot, const Notice &notice);
	void HideNotice(int slot);

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
