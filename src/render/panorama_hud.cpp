#include "panorama_hud.h"
#include "src/common.h"
#include "src/entity/ccscustomhudlayout.h"
#include "src/render/center_html.h"
#include "mmu/gamedata.h"
#include "mmu/log.h"
#include "mmu/sigscan.h"

#include <algorithm>
#include <cctype>
#include <checktransmitinfo.h>
#include <entity2/entitykeyvalues.h>
#include <entity2/entitysystem.h>
#include <filesystem.h>

#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unordered_map>

namespace
{
	using panorama_hud::Layout;

	struct LayoutDef
	{
		const char *file;
		const char *prefix;
		const char *name; // in the entity's targetname and the diagnostic
		int items;
		int nav;
	};

	constexpr int kLayoutCount = static_cast<int>(Layout::Count);
	// The layouts before the notice are the menus', one shown at a time.
	constexpr int kMenuLayouts = static_cast<int>(Layout::Notice);
	constexpr LayoutDef kLayouts[kLayoutCount] = {
		{"panorama/layout/custom_game/cs2menus/menu.vxml_c", "cm_", "list", panorama_hud::kItemSlots, panorama_hud::kNavSlots},
		{"panorama/layout/custom_game/cs2menus/grid.vxml_c", "cg_", "grid", panorama_hud::kGridSlots, panorama_hud::kGridTabs},
		{"panorama/layout/custom_game/cs2menus/showcase.vxml_c", "cx_", "showcase", panorama_hud::kShowcaseSlots, panorama_hud::kShowcaseTabs},
		{"panorama/layout/custom_game/cs2menus/studio.vxml_c", "cs_", "studio", panorama_hud::kShowcaseSlots, panorama_hud::kShowcaseTabs},
		{"panorama/layout/custom_game/cs2menus/columns.vxml_c", "cl_", "columns", panorama_hud::kColumnSlots, panorama_hud::kGridTabs},
		{"panorama/layout/custom_game/cs2menus/table.vxml_c", "ct_", "table", panorama_hud::kTableRows, panorama_hud::kTableKeys},
		{"panorama/layout/custom_game/cs2menus/notice.vxml_c", "cn_", "notice", 0, 0},
	};
	// Lets a reloaded plugin find the windows it left behind.
	constexpr const char *kTargetPrefix = "cs2menus_";

	using CreateEntityByName_t = CEntityInstance *(*)(const char *className, int forcedIndex);
	using DispatchSpawn_t = void (*)(CEntityInstance *entity, CEntityKeyValues *keyValues);
	using RemoveEntity_t = void (*)(CEntityInstance *entity);

	CreateEntityByName_t s_createEntity = nullptr;
	DispatchSpawn_t s_dispatchSpawn = nullptr;
	RemoveEntity_t s_removeEntity = nullptr;

	// CCSCustomHudLayout's networked state, written directly the way cs2kz-metamod does, so no game functions to sigscan.
	// Names go into the layout's string lists once, the state's entries point at them by index.
	struct HudOffsets
	{
		int32_t globalState = -1;   // CCSCustomHudLayout::m_globalLayoutState, embedded. Menus go there, see panorama_hud.h.
		int32_t panelIds = -1;      // CCSCustomHudLayout::m_vecPanelIds, CUtlString
		int32_t classNames = -1;    // CCSCustomHudLayout::m_vecClassNames, CUtlString
		int32_t variableNames = -1; // CCSCustomHudLayout::m_vecDialogVariableNames, CUtlString
		int32_t hasClasses = -1;    // CCSCustomHudLayoutState::m_vecHasClasses, HUDPanelHasClass_t
		int32_t variables = -1;     // CCSCustomHudLayoutState::m_vecDialogVariableStrings, HUDPanelDialogVariableString_t
		int32_t inputCapture = -1;  // CCSCustomHudLayoutState::m_bInputCaptureEnabled
		int32_t classPanel = -1, className = -1, classStatus = -1;                           // HUDPanelHasClass_t
		int32_t variablePanel = -1, variableName = -1, variableValue = -1, variableSet = -1; // HUDPanelDialogVariableString_t
		SchemaCollectionManipulatorFn_t panelIdsFn = nullptr, classNamesFn = nullptr, variableNamesFn = nullptr;
		SchemaCollectionManipulatorFn_t hasClassesFn = nullptr, variablesFn = nullptr;

		bool Ready() const
		{
			for (int32_t offset : {globalState, panelIds, classNames, variableNames, hasClasses, variables, inputCapture, classPanel, className,
								   classStatus, variablePanel, variableName, variableValue, variableSet})
			{
				if (offset < 0)
				{
					return false;
				}
			}
			return panelIdsFn && classNamesFn && variableNamesFn && hasClassesFn && variablesFn;
		}

		bool Resolve()
		{
			if (Ready())
			{
				return true;
			}
			// A struct's first field sits at 0, so FindOffset, which tells it from a missing one.
			auto field = [](const char *cls, const char *name) { return schema::FindOffset(cls, FNV1a(cls), name, FNV1a(name)); };
			auto list = [](const char *cls, const char *name) { return schema::GetCollectionManipulator(cls, FNV1a(cls), name, FNV1a(name)); };
			globalState = field("CCSCustomHudLayout", "m_globalLayoutState");
			panelIds = field("CCSCustomHudLayout", "m_vecPanelIds");
			classNames = field("CCSCustomHudLayout", "m_vecClassNames");
			variableNames = field("CCSCustomHudLayout", "m_vecDialogVariableNames");
			hasClasses = field("CCSCustomHudLayoutState", "m_vecHasClasses");
			variables = field("CCSCustomHudLayoutState", "m_vecDialogVariableStrings");
			inputCapture = field("CCSCustomHudLayoutState", "m_bInputCaptureEnabled");
			classPanel = field("HUDPanelHasClass_t", "m_nPanelIdIndex");
			className = field("HUDPanelHasClass_t", "m_nClassNameIndex");
			classStatus = field("HUDPanelHasClass_t", "m_eClassStatus");
			variablePanel = field("HUDPanelDialogVariableString_t", "m_nPanelIdIndex");
			variableName = field("HUDPanelDialogVariableString_t", "m_nDialogVariableIndex");
			variableValue = field("HUDPanelDialogVariableString_t", "m_sValue");
			variableSet = field("HUDPanelDialogVariableString_t", "m_bIsSet");
			panelIdsFn = list("CCSCustomHudLayout", "m_vecPanelIds");
			classNamesFn = list("CCSCustomHudLayout", "m_vecClassNames");
			variableNamesFn = list("CCSCustomHudLayout", "m_vecDialogVariableNames");
			hasClassesFn = list("CCSCustomHudLayoutState", "m_vecHasClasses");
			variablesFn = list("CCSCustomHudLayoutState", "m_vecDialogVariableStrings");
			return Ready();
		}
	} s_hud;

	// The game refuses to intern past this many names per list, and warns.
	constexpr int kMaxInterned = 1024;
	// More than one draw of the fullest layout adds: its fixed classes and a page of pictures.
	constexpr int kClassHeadroom = 160;

	template<typename T>
	T &FieldAt(void *base, int32_t offset)
	{
		return *reinterpret_cast<T *>(reinterpret_cast<uintptr_t>(base) + offset);
	}

	CCSCustomHudLayoutState *GlobalState(CCSCustomHudLayout *layout)
	{
		return &FieldAt<CCSCustomHudLayoutState>(layout, s_hud.globalState);
	}

	// The name's index in one of the layout's string lists, appended when new. -1 once the list is full.
	int Intern(CCSCustomHudLayout *layout, int32_t offset, SchemaCollectionManipulatorFn_t manipulator, const char *text)
	{
		schema::Collection<CUtlString> strings(&FieldAt<uint8_t>(layout, offset), manipulator);
		const int count = strings.Count();
		for (int i = 0; i < count; i++)
		{
			const char *entry = strings.At(i)->Get();
			if (entry && strcmp(entry, text) == 0)
			{
				return i;
			}
		}
		if (count >= kMaxInterned)
		{
			MMU_LOG_WARN("Panorama menu: can't add '%s', the layout already has %d names.\n", text, count);
			return -1;
		}
		CUtlString *added = strings.Append();
		if (!added)
		{
			return -1;
		}
		*added = text;
		layout->NetworkStateChanged(NetworkStateChangedData(true));
		return count;
	}

	// status 1 has the class, 0 hasn't.
	bool SetHasClass(CCSCustomHudLayout *layout, const char *panelId, const char *className, uint32_t status)
	{
		const int panel = Intern(layout, s_hud.panelIds, s_hud.panelIdsFn, panelId);
		const int name = Intern(layout, s_hud.classNames, s_hud.classNamesFn, className);
		if (panel < 0 || name < 0)
		{
			return false;
		}
		CCSCustomHudLayoutState *state = GlobalState(layout);
		schema::Collection<uint8_t> entries(&FieldAt<uint8_t>(state, s_hud.hasClasses), s_hud.hasClassesFn);
		const int count = entries.Count();
		for (int i = 0; i < count; i++)
		{
			uint8_t *entry = entries.At(i);
			if (FieldAt<uint16_t>(entry, s_hud.classPanel) == panel && FieldAt<uint16_t>(entry, s_hud.className) == name)
			{
				FieldAt<uint32_t>(entry, s_hud.classStatus) = status;
				state->Notify(NetworkStateChangedData(static_cast<uint32>(s_hud.hasClasses), i));
				return true;
			}
		}
		uint8_t *added = entries.Append();
		if (!added)
		{
			return false;
		}
		FieldAt<uint16_t>(added, s_hud.classPanel) = static_cast<uint16_t>(panel);
		FieldAt<uint16_t>(added, s_hud.className) = static_cast<uint16_t>(name);
		FieldAt<uint32_t>(added, s_hud.classStatus) = status;
		state->MarkChanged();
		return true;
	}

	bool SetDialogVariable(CCSCustomHudLayout *layout, const char *panelId, const char *variable, const char *value)
	{
		const int panel = Intern(layout, s_hud.panelIds, s_hud.panelIdsFn, panelId);
		const int name = Intern(layout, s_hud.variableNames, s_hud.variableNamesFn, variable);
		if (panel < 0 || name < 0)
		{
			return false;
		}
		CCSCustomHudLayoutState *state = GlobalState(layout);
		schema::Collection<uint8_t> entries(&FieldAt<uint8_t>(state, s_hud.variables), s_hud.variablesFn);
		const int count = entries.Count();
		for (int i = 0; i < count; i++)
		{
			uint8_t *entry = entries.At(i);
			if (FieldAt<uint16_t>(entry, s_hud.variablePanel) == panel && FieldAt<uint16_t>(entry, s_hud.variableName) == name)
			{
				FieldAt<CUtlString>(entry, s_hud.variableValue) = value;
				FieldAt<bool>(entry, s_hud.variableSet) = true;
				state->Notify(NetworkStateChangedData(static_cast<uint32>(s_hud.variables), i));
				return true;
			}
		}
		uint8_t *added = entries.Append();
		if (!added)
		{
			return false;
		}
		FieldAt<uint16_t>(added, s_hud.variablePanel) = static_cast<uint16_t>(panel);
		FieldAt<uint16_t>(added, s_hud.variableName) = static_cast<uint16_t>(name);
		FieldAt<CUtlString>(added, s_hud.variableValue) = value;
		FieldAt<bool>(added, s_hud.variableSet) = true;
		state->MarkChanged();
		return true;
	}

	bool s_mounted[kLayoutCount] = {};
	// Clicks decoded from any layout, so the diagnostic can tell a dead click hook from an unclicked menu.
	int s_clicksSeen = 0;

	bool Resolved()
	{
		return s_createEntity && s_dispatchSpawn && s_removeEntity && s_hud.Ready();
	}

	// The layout rejects markup, so colors are classes. Keep in step with the cm-col rules in menu.css.
	// The first 16 are the chat color codes 0x01-0x10 as center_html renders them.
	struct PaletteColor
	{
		const char *className;
		int r, g, b;
	};

	constexpr PaletteColor kPalette[] = {
		{"cm-col1", 0xFF, 0xFF, 0xFF},  {"cm-col2", 0xC0, 0x30, 0x30},  {"cm-col3", 0xFF, 0x66, 0x99},  {"cm-col4", 0x4C, 0xAF, 0x50},
		{"cm-col5", 0x9E, 0xB8, 0x25},  {"cm-col6", 0x66, 0xCC, 0x66},  {"cm-col7", 0xCC, 0x30, 0x30},  {"cm-col8", 0xCC, 0xCC, 0xCC},
		{"cm-col9", 0xFF, 0xFF, 0x00},  {"cm-col10", 0xB0, 0xC4, 0xDE}, {"cm-col11", 0x50, 0x70, 0xFF}, {"cm-col12", 0x30, 0x50, 0xC0},
		{"cm-col13", 0x80, 0x90, 0xA0}, {"cm-col14", 0xE0, 0x60, 0xC0}, {"cm-col15", 0xFF, 0x50, 0x50}, {"cm-col16", 0xFF, 0xD7, 0x00},
		{"cm-col17", 0x70, 0x70, 0x70}, {"cm-col18", 0xA0, 0xA0, 0xA0}, {"cm-col19", 0x00, 0x00, 0x00}, {"cm-col20", 0xFF, 0x2E, 0xE7},
	};

	const char *ColorClass(const std::string &hex)
	{
		// "#RRGGBB" or "#RRGGBBAA". The palette has no alpha, so it's dropped.
		unsigned int rgb = 0xFFFFFF;
		if ((hex.size() == 7 || hex.size() == 9) && hex[0] == '#')
		{
			rgb = static_cast<unsigned int>(strtoul(hex.substr(1, 6).c_str(), nullptr, 16));
		}
		const int r = (rgb >> 16) & 0xFF;
		const int g = (rgb >> 8) & 0xFF;
		const int b = rgb & 0xFF;
		const PaletteColor *best = &kPalette[0];
		int bestDistance = INT_MAX;
		for (const PaletteColor &color : kPalette)
		{
			const int distance = (color.r - r) * (color.r - r) + (color.g - g) * (color.g - g) + (color.b - b) * (color.b - b);
			if (distance < bestDistance)
			{
				best = &color;
				bestDistance = distance;
			}
		}
		return best->className;
	}

	const char *ControlClass(panorama_hud::View::Control control)
	{
		switch (control)
		{
			case panorama_hud::View::Control::Toggle:
				return "type-toggle";
			case panorama_hud::View::Control::Stepper:
				return "type-step";
			case panorama_hud::View::Control::Choice:
				return "type-choice";
			default:
				return "";
		}
	}

	struct Window
	{
		CEntityHandle entity;
		bool shown = false;
		bool capture = false;
		// Last written values, keyed "panel class" and "panel var", so unchanged writes are skipped.
		std::unordered_map<std::string, bool> classes;
		std::unordered_map<std::string, std::string> vars;
		// The one class per panel from a set, like its font or color, keyed by panel.
		std::unordered_map<std::string, std::string> swaps;
	};

	Window s_windows[MAXPLAYERS][kLayoutCount];

	void *FindSig(void *base, size_t size, const char *signature, const char *name)
	{
		bool multiple = false;
		void *address = sig::FindSignatureUnique(base, size, signature, multiple);
		if (!address || multiple)
		{
			MMU_LOG_WARN("%s signature %s - panorama menus disabled.\n", name, address ? "matched multiple times" : "not found");
			return nullptr;
		}
		return address;
	}

	CCSCustomHudLayout *GetLayout(int slot, int layout)
	{
		const CEntityHandle &handle = s_windows[slot][layout].entity;
		if (!handle.IsValid() || !g_pEntitySystem)
		{
			return nullptr;
		}
		return static_cast<CCSCustomHudLayout *>(g_pEntitySystem->GetEntityInstance(handle));
	}

	CCSCustomHudLayout *EnsureLayout(int slot, int layout)
	{
		if (CCSCustomHudLayout *existing = GetLayout(slot, layout))
		{
			return existing;
		}
		// The caches belonged to an entity that's gone.
		s_windows[slot][layout] = Window {};
		if (!g_pEntitySystem || !panorama_hud::Available(static_cast<Layout>(layout)))
		{
			return nullptr;
		}
		CEntityInstance *entity = s_createEntity("custom_hud_layout", -1);
		if (!entity)
		{
			return nullptr;
		}
		char name[48];
		snprintf(name, sizeof(name), "%s%s%d", kTargetPrefix, kLayouts[layout].name, slot);
		CEntityKeyValues *keyValues = new CEntityKeyValues();
		keyValues->SetString("layout", kLayouts[layout].file);
		keyValues->SetString("targetname", name);
		s_dispatchSpawn(entity, keyValues);
		s_windows[slot][layout].entity = entity->GetRefEHandle();
		return static_cast<CCSCustomHudLayout *>(entity);
	}

	bool SetCapture(int slot, Window &window, CCSCustomHudLayout *layout, bool enabled)
	{
		schema::Collection<CCSCustomHudLayoutState> states = layout->m_vecPlayerLayoutStates();
		const int16_t slotOffset = CCSCustomHudLayoutState::m_playerSlot_Offset();
		if (slot >= states.Count() || slotOffset <= 0)
		{
			return false;
		}
		CCSCustomHudLayoutState *state = states.At(slot);
		if (!state)
		{
			return false;
		}
		// Entries start stamped with slot 0, the game only resolves a state's capture past slot 0 once it's restamped.
		CPlayerSlot &stamped = *reinterpret_cast<CPlayerSlot *>(reinterpret_cast<uintptr_t>(state) + slotOffset);
		if (stamped.Get() != slot)
		{
			stamped = CPlayerSlot(slot);
			state->MarkChanged();
		}
		bool &capture = FieldAt<bool>(state, s_hud.inputCapture);
		if (capture != enabled)
		{
			capture = enabled;
			state->Notify(NetworkStateChangedData(static_cast<uint32>(s_hud.inputCapture)));
		}
		window.capture = enabled;
		return true;
	}

	// One window's diff-cached writes. Ids are the layout prefix plus a suffix, a label's dialog variable is named after it.
	struct Writer
	{
		Window &window;
		CCSCustomHudLayout *entity;
		const char *prefix;

		std::string Id(const char *suffix) const
		{
			return prefix + std::string(suffix);
		}

		std::string Id(const char *format, int a) const
		{
			char suffix[32];
			snprintf(suffix, sizeof(suffix), format, a);
			return prefix + std::string(suffix);
		}

		std::string Id(const char *format, int a, int b) const
		{
			char suffix[32];
			snprintf(suffix, sizeof(suffix), format, a, b);
			return prefix + std::string(suffix);
		}

		void Class(const std::string &panel, const char *cls, bool present)
		{
			const std::string key = panel + ' ' + cls;
			auto it = window.classes.find(key);
			if (it != window.classes.end() && it->second == present)
			{
				return;
			}
			SetHasClass(entity, panel.c_str(), cls, present ? 1 : 0);
			window.classes[key] = present;
		}

		void Var(const std::string &panel, const std::string &value)
		{
			const std::string key = panel + ' ' + panel;
			auto it = window.vars.find(key);
			if (it != window.vars.end() && it->second == value)
			{
				return;
			}
			// A word joiner first, or the client localizes a value starting with '#', like a player named "#1".
			SetDialogVariable(entity, panel.c_str(), panel.c_str(), ("\xE2\x81\xA0" + value).c_str());
			window.vars[key] = value;
		}

		// Replaces the panel's previous class from the same set, a panel's sets named apart. Empty removes it.
		void Swap(const std::string &panel, const std::string &cls, const char *set = "")
		{
			std::string &current = window.swaps[*set ? panel + '#' + set : panel];
			if (current == cls)
			{
				return;
			}
			if (!current.empty())
			{
				Class(panel, current.c_str(), false);
			}
			if (!cls.empty())
			{
				Class(panel, cls.c_str(), true);
			}
			current = cls;
		}

		// Written names stay interned, so untouched panels are left alone.
		bool Touched(const std::string &panel, const char *cls) const
		{
			return window.classes.count(panel + ' ' + cls) != 0;
		}

		bool VarTouched(const std::string &panel) const
		{
			return window.vars.count(panel + ' ' + panel) != 0;
		}

		// A class only some panels ever get: written once it's there, and from then on.
		void Flag(const std::string &panel, const char *cls, bool present)
		{
			if (present || Touched(panel, cls))
			{
				Class(panel, cls, present);
			}
		}

		// The same for a label most panels leave empty.
		void Text(const std::string &panel, const std::string &value)
		{
			if (!value.empty() || VarTouched(panel))
			{
				Var(panel, value);
			}
		}

		void Control(const std::string &panel, const panorama_hud::View::Row &row)
		{
			Class(panel, "disabled", row.disabled);
			Swap(panel, ControlClass(row.control));
			const bool toggle = row.control == panorama_hud::View::Control::Toggle;
			if (toggle || Touched(panel, "on"))
			{
				Class(panel, "on", toggle && row.on);
			}
		}
	};

	// A tile's name is one label, in the color its text starts with.
	std::string RowName(const panorama_hud::View::Row &row)
	{
		std::string name;
		for (const panorama_hud::View::Segment &segment : row.segments)
		{
			name += segment.text;
		}
		return name;
	}

	// As MenuTone, "" for Info.
	const char *ToneClass(int tone)
	{
		static const char *const kTones[] = {"", "tone-ok", "tone-warn", "tone-bad"};
		return tone > 0 && tone < 4 ? kTones[tone] : "";
	}

	// Key caps and what they do: a part of the hint pill, a row of the key list. The ids take the row, and the key in it.
	void WriteKeys(Writer &w, const panorama_hud::View::HintPart &part, int row, int maxKeys, const char *keyId, const char *textId)
	{
		for (int k = 0; k < maxKeys; k++)
		{
			const bool key = k < static_cast<int>(part.keys.size());
			const std::string label = w.Id(keyId, row, k);
			if (key)
			{
				w.Var(label, part.keys[k]);
			}
			w.Class(label, "hidden", !key);
		}
		w.Var(w.Id(textId, row), part.text);
	}

	// What a button and a studio control share: a heading, a segment of a Choice, a readout, the accent and the span.
	template<typename T>
	void WriteButtonKind(Writer &w, const std::string &panel, const T &button)
	{
		w.Flag(panel, "heading", button.heading);
		w.Flag(panel, "seg", button.segCount > 0);
		w.Flag(panel, "seg-first", button.segCount > 0 && button.segIndex == 0);
		w.Flag(panel, "seg-last", button.segCount > 0 && button.segIndex == button.segCount - 1);
		w.Swap(panel, button.segCount > 0 ? "seg-n" + std::to_string(button.segCount) : std::string(), "segments");
		w.Class(panel, "readout", button.readout);
		w.Class(panel, "highlight", button.highlight);
		w.Swap(panel, button.span >= 3 ? "span3" : button.span == 2 ? "span2" : "", "span");
	}

	void WriteListRows(Writer &w, const panorama_hud::View &view)
	{
		for (int i = 0; i < panorama_hud::kItemSlots; i++)
		{
			const bool used = i < static_cast<int>(view.rows.size());
			const std::string panel = w.Id("item%d", i);
			if (used)
			{
				const panorama_hud::View::Row &row = view.rows[i];
				for (int s = 0; s < panorama_hud::kRowSegments; s++)
				{
					// Unused runs are emptied, not hidden, so they take no width and keep their last class.
					const std::string label = w.Id("seg%d_%d", i, s);
					const bool filled = s < static_cast<int>(row.segments.size());
					w.Var(label, filled ? row.segments[s].text : std::string());
					if (filled)
					{
						w.Swap(label, ColorClass(row.segments[s].color));
					}
				}
				const std::string value = w.Id("val%d", i);
				w.Var(value, row.value);
				if (!row.segments.empty())
				{
					w.Swap(value, ColorClass(row.segments[0].color));
				}
				w.Control(panel, row);
			}
			w.Class(panel, "hidden", !used);
		}
	}

	// Classes on the tile's button, its badge panels have no ids. Nothing is written for a tile that never had a badge.
	// `tag` is the label the tile's tag goes in.
	void WriteBadges(Writer &w, const std::string &panel, const std::string &tag, const panorama_hud::View::Row &row)
	{
		static const char *const kCorners[] = {"", "corner-star", "corner-star-on", "corner-star-undo", "corner-copy"};
		w.Swap(panel, row.rarity.empty() ? std::string() : "rar-" + row.rarity, "rarity");
		w.Flag(panel, "team-t", (row.teams & 1) != 0);
		w.Flag(panel, "team-ct", (row.teams & 2) != 0);
		w.Flag(panel, "locked", row.locked);
		w.Swap(panel, kCorners[static_cast<int>(row.corner)], "corner");
		w.Flag(panel, "tagged", !row.tag.empty());
		if (!row.tag.empty() || w.VarTouched(tag))
		{
			w.Var(tag, row.tag);
			w.Swap(tag, row.tagStyle.empty() ? std::string() : "tag-" + row.tagStyle);
		}
	}

	void WriteMessage(Writer &w, const panorama_hud::View &view)
	{
		const std::string panel = w.Id("msg");
		w.Class(panel, "hidden", view.message.empty());
		if (!view.message.empty())
		{
			w.Var(w.Id("msg_text"), view.message);
			w.Swap(panel, ToneClass(view.messageTone));
		}
	}

	// A whole percentage as its width class.
	std::string Percent(int value)
	{
		return "w-" + std::to_string((std::max)(0, (std::min)(100, value)));
	}

	void WriteInfo(Writer &w, const panorama_hud::View &view)
	{
		const panorama_hud::View::Info &info = view.info;
		const std::string panel = w.Id("info");
		w.Class(panel, "hidden", !info.shown);
		if (!info.shown)
		{
			return;
		}
		w.Var(w.Id("info_title"), info.title);
		const std::string sub = w.Id("info_sub");
		w.Var(sub, info.subtitle);
		const std::string &color = info.subtitleColor;
		w.Swap(sub, color.empty() ? std::string() : color[0] == '#' ? std::string(ColorClass(color)) : "rar-" + color);
		const std::string meter = w.Id("info_meter");
		w.Class(meter, "hidden", !info.meter);
		if (info.meter)
		{
			w.Class(meter, "plain", info.bands.empty());
			for (int i = 0; i < panorama_hud::kInfoBands; i++)
			{
				const int width = info.bands.empty() ? (i == 0 ? 100 : 0) : i < static_cast<int>(info.bands.size()) ? info.bands[i] : 0;
				w.Swap(w.Id("info_band%d", i), Percent(width));
			}
			w.Swap(w.Id("info_dim_l"), Percent(info.rangeLo));
			w.Swap(w.Id("info_gap"), Percent(info.rangeHi - info.rangeLo));
			w.Swap(w.Id("info_dim_r"), Percent(100 - info.rangeHi));
			w.Swap(w.Id("info_mark_sp"), Percent(info.mark));
			w.Var(w.Id("info_mlabel"), info.meterLabel);
			w.Var(w.Id("info_mvalue"), info.meterValue);
		}
		// Two short rows to a line. The second one's value ends at the card's edge, "end" says which that is.
		bool second = false;
		for (int i = 0; i < panorama_hud::kInfoRows; i++)
		{
			const bool used = i < static_cast<int>(info.rows.size());
			const std::string row = w.Id("info_row%d", i);
			if (used)
			{
				const bool wide = info.rows[i].wide;
				w.Var(w.Id("info_rl%d", i), info.rows[i].label);
				w.Var(w.Id("info_rv%d", i), info.rows[i].value);
				w.Flag(row, "wide", wide);
				w.Flag(row, "end", !wide && second);
				second = !wide && !second;
			}
			w.Class(row, "hidden", !used);
		}
	}

	void WriteDialog(Writer &w, const panorama_hud::View &view)
	{
		const std::string panel = w.Id("dlg");
		w.Class(panel, "hidden", !view.dialog.open);
		if (!view.dialog.open)
		{
			return;
		}
		w.Class(panel, "danger", view.dialog.danger);
		w.Var(w.Id("dlg_title"), view.dialog.title);
		w.Var(w.Id("dlg_body"), view.dialog.body);
		w.Var(w.Id("dlg_no_lbl"), view.dialog.cancel);
		w.Var(w.Id("dlg_yes_lbl"), view.dialog.confirm);
	}

	void WriteInput(Writer &w, const panorama_hud::View &view)
	{
		const panorama_hud::View::Input &input = view.input;
		const std::string panel = w.Id("input");
		w.Class(panel, "hidden", !input.shown);
		if (!input.shown)
		{
			return;
		}
		w.Class(panel, "typing", input.typing);
		w.Class(panel, "overlay", input.overlay);
		w.Class(panel, "placeholder", input.placeholder);
		w.Class(panel, "clearable", input.clear && !input.typing);
		w.Var(w.Id("input_text"), input.text);
		w.Var(w.Id("input_hint"), input.hint);
	}

	void WriteColumns(Writer &w, const panorama_hud::View &view)
	{
		for (int c = 0; c < panorama_hud::kColumns; c++)
		{
			const bool used = c < static_cast<int>(view.columns.size());
			if (used)
			{
				w.Var(w.Id("colh%d", c), view.columns[c].label);
				w.Var(w.Id("colc%d", c), view.columns[c].count > 0 ? std::to_string(view.columns[c].count) : std::string());
			}
			w.Class(w.Id("col%d", c), "hidden", !used);
		}
		const panorama_hud::View::Row *slots[panorama_hud::kColumnSlots] = {};
		for (const panorama_hud::View::Row &row : view.rows)
		{
			if (row.slot >= 0 && row.slot < panorama_hud::kColumnSlots)
			{
				slots[row.slot] = &row;
			}
		}
		for (int i = 0; i < panorama_hud::kColumnSlots; i++)
		{
			const std::string panel = w.Id("item%d", i);
			if (const panorama_hud::View::Row *row = slots[i])
			{
				w.Var(w.Id("name%d", i), RowName(*row));
				w.Var(w.Id("val%d", i), row->value);
				const std::string image = w.Id("img%d", i);
				w.Swap(image, row->image.empty() ? std::string() : "img-" + row->image);
				w.Swap(image, row->imageTint.empty() ? std::string() : "tint-" + row->imageTint, "tint");
				WriteBadges(w, panel, w.Id("tag%d", i), *row);
				w.Class(panel, "half", row->half);
				w.Class(panel, "heading", row->heading);
				w.Class(panel, "disabled", row->disabled);
			}
			w.Class(panel, "hidden", !slots[i]);
		}
	}

	void WriteChips(Writer &w, const panorama_hud::View &view)
	{
		w.Class(w.Id("chips"), "hidden", view.chips.empty());
		for (int i = 0; i < panorama_hud::kChipSlots; i++)
		{
			const bool used = i < static_cast<int>(view.chips.size());
			const std::string panel = w.Id("chip%d", i);
			if (used)
			{
				const panorama_hud::View::Chip &chip = view.chips[i];
				w.Var(w.Id("chip_lbl%d", i), chip.label);
				w.Var(w.Id("chip_val%d", i), chip.value);
				w.Class(panel, "on", chip.on);
				w.Class(panel, "menu", chip.menu);
				w.Class(panel, "valued", !chip.value.empty());
				w.Flag(panel, "note", chip.note);
			}
			w.Class(panel, "hidden", !used);
		}
	}

	// How many characters a name shows as: code points, without the zero width and soft hyphen ones.
	size_t ShownLength(const std::string &text)
	{
		size_t n = 0;
		for (size_t i = 0; i < text.size(); i++)
		{
			const unsigned char c = static_cast<unsigned char>(text[i]);
			if ((c & 0xC0) == 0x80)
			{
				continue;
			}
			const bool zeroWidth = c == 0xE2 && i + 2 < text.size() && static_cast<unsigned char>(text[i + 1]) == 0x80
								   && (static_cast<unsigned char>(text[i + 2]) & 0xFC) == 0x8C;
			const bool softHyphen = c == 0xC2 && i + 1 < text.size() && static_cast<unsigned char>(text[i + 1]) == 0xAD;
			n += zeroWidth || softHyphen ? 0 : 1;
		}
		return n;
	}

	// The whole name on hover, for a tile whose two lines cut it at `fits` characters. Emptied otherwise: a tile without
	// an image shows the label, for the room it takes.
	void WriteLongName(Writer &w, const std::string &panel, int i, const std::string &name, bool tile, size_t fits)
	{
		const bool cut = tile && ShownLength(name) > fits;
		w.Flag(panel, "long", cut);
		w.Text(w.Id("full%d", i), cut ? name : std::string());
	}

	// Buttons flow three columns to a line and wrap. A heading takes a whole line, and so do a Choice's segments together,
	// which the layout can't force after a line that isn't full: the button before them takes what's left of it as a margin.
	struct ButtonFlow
	{
		std::vector<int> line; // the line each button lands on
		std::vector<int> fill; // the columns each leaves empty after it, 0 for most
	};

	template<typename T>
	ButtonFlow FlowOf(const std::vector<T> &buttons, int count)
	{
		ButtonFlow flow;
		flow.line.assign(count, 0);
		flow.fill.assign(count, 0);
		for (int i = 0, at = 0, used = 0; i < count; i++)
		{
			const T &button = buttons[i];
			const bool segment = button.segCount > 0;
			const int width = button.heading || segment ? 3 : (std::max)(1, (std::min)(button.span, 3));
			// A Choice's further segments stay on the line its first began.
			if (!(segment && button.segIndex > 0) && used > 0 && used + width > 3)
			{
				flow.fill[i - 1] = segment ? 3 - used : 0;
				at++;
				used = 0;
			}
			flow.line[i] = at;
			used = segment ? 3 : used + width;
			if (used >= 3 && (!segment || button.segIndex == button.segCount - 1))
			{
				at++;
				used = 0;
			}
		}
		return flow;
	}

	const char *FillClass(int columns)
	{
		return columns >= 2 ? "fill2" : columns == 1 ? "fill1" : "";
	}

	void WritePager(Writer &w, const panorama_hud::View &view)
	{
		// Hidden in the layout, so only written once there's been a page to show.
		const std::string pager = w.Id("pager");
		if (!view.page.empty() || w.Touched(pager, "hidden"))
		{
			w.Class(pager, "hidden", view.page.empty());
		}
		if (!view.page.empty())
		{
			w.Var(w.Id("page"), view.page);
			w.Class(w.Id("prev"), "disabled", !view.prev);
			w.Class(w.Id("next"), "disabled", !view.next);
		}
	}

	// The grid's tiles. One without an image has its text in the middle, its tag as a line under the name, and a short
	// name, like a number, large.
	void WriteTiles(Writer &w, const panorama_hud::View &view)
	{
		w.Swap(w.Id("tiles"), view.tiles == panorama_hud::TileSize::Cards    ? "size-xl"
							  : view.tiles == panorama_hud::TileSize::Large  ? "size-l"
							  : view.tiles == panorama_hud::TileSize::Medium ? "size-m"
																			 : "");
		// What a tile's two lines hold, in characters.
		const size_t fits = view.tiles == panorama_hud::TileSize::Small ? 28 : view.tiles == panorama_hud::TileSize::Medium ? 44 : 60;
		for (int i = 0; i < panorama_hud::kGridSlots; i++)
		{
			const bool used = i < static_cast<int>(view.rows.size());
			const std::string panel = w.Id("item%d", i);
			if (used)
			{
				const panorama_hud::View::Row &row = view.rows[i];
				const std::string name = RowName(row);
				const char *color = ColorClass(row.segments.empty() ? std::string() : row.segments[0].color);
				const std::string nameLabel = w.Id("name%d", i);
				w.Var(nameLabel, name);
				w.Swap(nameLabel, color);
				const std::string value = w.Id("val%d", i);
				w.Var(value, row.value);
				w.Swap(value, color);
				const bool plain = row.image.empty();
				w.Flag(panel, "noimg", plain);
				w.Flag(panel, "big", plain && panorama_hud::TextCells(name) <= 6);
				w.Flag(panel, "aux", plain && !row.tag.empty());
				WriteLongName(w, panel, i, name, !plain, fits);
				const std::string image = w.Id("img%d", i);
				w.Swap(image, plain ? std::string() : "img-" + row.image);
				w.Swap(image, row.imageTint.empty() ? std::string() : "tint-" + row.imageTint, "tint");
				// The tag's place is the line under the name there, in its style's color.
				const std::string aux = w.Id("aux%d", i);
				w.Text(aux, plain ? row.tag : std::string());
				if (plain)
				{
					w.Swap(aux, row.tagStyle.empty() ? std::string() : "tag-" + row.tagStyle);
					panorama_hud::View::Row untagged = row;
					untagged.tag.clear();
					WriteBadges(w, panel, w.Id("tag%d", i), untagged);
				}
				else
				{
					WriteBadges(w, panel, w.Id("tag%d", i), row);
				}
				w.Flag(panel, "highlight", row.highlight);
				w.Control(panel, row);
			}
			w.Class(panel, "hidden", !used);
		}
		WritePager(w, view);
	}

	// The showcase's and the studio's buttons, three columns of them. An item with an image is a tile with every badge and
	// the rest of its row is as tall, one without takes the tag at its name's end and the rarity down its edge.
	void WriteButtons(Writer &w, const panorama_hud::View &view)
	{
		const int count = (std::min)(static_cast<int>(view.rows.size()), panorama_hud::kShowcaseSlots);
		auto pictured = [&view](int i)
		{
			const panorama_hud::View::Row &row = view.rows[i];
			return !row.image.empty() && !row.heading && row.segCount == 0;
		};
		// A line with a tile on it is as tall as the tile all along.
		const ButtonFlow flow = FlowOf(view.rows, count);
		std::vector<bool> tallLine(count, false);
		for (int i = 0; i < count; i++)
		{
			tallLine[flow.line[i]] = tallLine[flow.line[i]] || pictured(i);
		}
		for (int i = 0; i < panorama_hud::kShowcaseSlots; i++)
		{
			const bool used = i < count;
			const std::string panel = w.Id("item%d", i);
			if (used)
			{
				const panorama_hud::View::Row &row = view.rows[i];
				const std::string name = RowName(row);
				const char *color = ColorClass(row.segments.empty() ? std::string() : row.segments[0].color);
				// A heading and a segment take their color from the styles.
				const std::string nameLabel = w.Id("name%d", i);
				w.Var(nameLabel, name);
				w.Swap(nameLabel, row.heading || row.segCount > 0 ? "" : color);
				const std::string value = w.Id("val%d", i);
				w.Var(value, row.value);
				w.Swap(value, color);
				const bool pic = pictured(i);
				w.Flag(panel, "pic", pic);
				w.Flag(panel, "tall", !pic && !row.heading && row.segCount == 0 && tallLine[flow.line[i]]);
				w.Swap(panel, FillClass(flow.fill[i]), "fill");
				WriteLongName(w, panel, i, name, pic, 36);
				if (pic)
				{
					// The tile's own labels, see buttons.css.
					const std::string imageName = w.Id("iname%d", i);
					w.Var(imageName, name);
					w.Swap(imageName, color);
					const std::string imageValue = w.Id("ival%d", i);
					w.Var(imageValue, row.value);
					w.Swap(imageValue, color);
				}
				const std::string image = w.Id("img%d", i);
				w.Swap(image, pic ? "img-" + row.image : std::string());
				w.Swap(image, pic && !row.imageTint.empty() ? "tint-" + row.imageTint : std::string(), "tint");
				if (pic)
				{
					WriteBadges(w, panel, w.Id("tag%d", i), row);
				}
				else
				{
					panorama_hud::View::Row text;
					text.tag = row.tag;
					text.tagStyle = row.tagStyle;
					WriteBadges(w, panel, w.Id("btag%d", i), text);
				}
				w.Swap(panel, !pic && !row.rarity.empty() ? "stripe-" + row.rarity : std::string(), "stripe");
				w.Flag(panel, "two",
					   !pic && !row.heading && row.segCount == 0 && !row.readout && !row.value.empty() && (row.span > 1 || !row.tag.empty()));
				WriteButtonKind(w, panel, row);
				w.Control(panel, row);
			}
			w.Class(panel, "hidden", !used);
		}
		WritePager(w, view);
	}

	// The table's headings and rows. The cells under a heading are set apart from the ones before by a gap.
	void WriteTable(Writer &w, const panorama_hud::View &view)
	{
		w.Flag(w.Id("root"), "more", view.details);
		// One page: the window is only as tall as its rows.
		w.Flag(w.Id("root"), "fit", view.page.empty());
		bool first[panorama_hud::kTableCells] = {};
		int cellCount = 0;
		for (int i = 0; i < panorama_hud::kTableColumns; i++)
		{
			const bool used = i < static_cast<int>(view.heads.size());
			const std::string panel = w.Id("head%d", i);
			if (used)
			{
				const panorama_hud::View::Head &head = view.heads[i];
				w.Var(w.Id("head_lbl%d", i), head.label);
				w.Swap(panel, i > 0 ? "w" + std::to_string(head.cells) : std::string(), "width");
				w.Class(panel, "asc", head.sort > 0);
				w.Class(panel, "desc", head.sort < 0);
				w.Class(panel, "button", view.headButtons);
				if (i > 0 && cellCount < panorama_hud::kTableCells)
				{
					first[cellCount] = true;
					cellCount = (std::min)(cellCount + head.cells, panorama_hud::kTableCells);
				}
			}
			w.Class(panel, "hidden", !used);
		}
		for (int i = 0; i < panorama_hud::kTableRows; i++)
		{
			const bool used = i < static_cast<int>(view.rows.size());
			const std::string panel = w.Id("item%d", i);
			if (used)
			{
				const panorama_hud::View::Row &row = view.rows[i];
				const std::string name = w.Id("name%d", i);
				w.Var(name, RowName(row));
				if (!row.segments.empty())
				{
					w.Swap(name, ColorClass(row.segments[0].color));
				}
				for (int k = 0; k < panorama_hud::kTableCells; k++)
				{
					const std::string cell = w.Id("c%d_%d", i, k);
					const bool filled = k < static_cast<int>(row.cells.size());
					if (k < cellCount)
					{
						w.Var(cell, filled ? row.cells[k].text : std::string());
						if (filled)
						{
							w.Swap(cell, ColorClass(row.cells[k].color));
						}
					}
					w.Flag(cell, "first", k < cellCount && first[k]);
					w.Class(cell, "hidden", k >= cellCount);
				}
				w.Flag(w.Id("corner%d", i), "on", row.details);
				w.Class(panel, "disabled", row.disabled);
			}
			w.Class(panel, "hidden", !used);
		}
		WritePager(w, view);
	}

	void WritePopups(Writer &w, const panorama_hud::View &view)
	{
		const bool popup = view.step.open || view.list.open;
		w.Class(w.Id("root"), "shift", popup);
		// The showcase draws the image inside, the studio has none, the others beside the box.
		if (view.layout != Layout::Showcase && view.layout != Layout::Studio)
		{
			const bool preview = !popup && !view.image.empty();
			w.Class(w.Id("root"), "preview", preview);
			const std::string previewPanel = w.Id("preview");
			if (preview || w.Touched(previewPanel, "hidden"))
			{
				w.Class(previewPanel, "hidden", !preview);
			}
			if (preview)
			{
				w.Swap(w.Id("preview_img"), "img-" + view.image);
			}
		}
		const std::string step = w.Id("step");
		if (view.step.open || w.Touched(step, "hidden"))
		{
			w.Class(step, "hidden", !view.step.open);
		}
		if (view.step.open)
		{
			w.Var(w.Id("step_title"), view.step.title);
			w.Var(w.Id("step_val"), view.step.readout);
			for (int i = 0; i < panorama_hud::kStepButtons; i++)
			{
				const panorama_hud::View::StepButton &button = view.step.buttons[i];
				const std::string panel = w.Id("step_b%d", i);
				w.Var(w.Id("step_l%d", i), button.label);
				w.Class(panel, "disabled", !button.enabled);
				w.Class(panel, "hidden", button.label.empty());
			}
		}
		const std::string list = w.Id("list");
		if (view.list.open || w.Touched(list, "hidden"))
		{
			w.Class(list, "hidden", !view.list.open);
		}
		if (view.list.open)
		{
			w.Var(w.Id("list_title"), view.list.title);
			for (int i = 0; i < panorama_hud::kListSlots; i++)
			{
				const bool used = i < static_cast<int>(view.list.rows.size());
				const std::string panel = w.Id("li%d", i);
				if (used)
				{
					const panorama_hud::View::ListRow &row = view.list.rows[i];
					w.Var(w.Id("li_lbl%d", i), row.label);
					w.Class(panel, "selected", row.selected);
					w.Flag(panel, "marked", row.marked);
					w.Swap(panel, ToneClass(row.tone), "tone");
					// The list layout's rows have no second line.
					if (view.layout != Layout::List)
					{
						w.Text(w.Id("li_sub%d", i), row.sub);
						w.Flag(panel, "two", !row.sub.empty());
					}
				}
				w.Class(panel, "hidden", !used);
			}
			w.Class(list, "paged", !view.list.page.empty());
			if (!view.list.page.empty())
			{
				w.Var(w.Id("list_page"), view.list.page);
				w.Class(w.Id("list_prev"), "disabled", !view.list.prev);
				w.Class(w.Id("list_next"), "disabled", !view.list.next);
			}
		}
	}

	void HideWindow(int slot, int layout)
	{
		Window &window = s_windows[slot][layout];
		if (!window.shown)
		{
			return;
		}
		window.shown = false;
		if (CCSCustomHudLayout *entity = GetLayout(slot, layout))
		{
			Writer w {window, entity, kLayouts[layout].prefix};
			w.Class(w.Id("root"), "hidden", true);
			SetCapture(slot, window, entity, false);
		}
	}

	bool ReadVarint(const uint8_t *&p, const uint8_t *end, uint64_t &out)
	{
		out = 0;
		for (int shift = 0; shift < 64 && p < end; shift += 7)
		{
			const uint8_t byte = *p++;
			out |= static_cast<uint64_t>(byte & 0x7F) << shift;
			if (!(byte & 0x80))
			{
				return true;
			}
		}
		return false;
	}

	// "item7" gives 7. "item_lbl7" and anything else give false.
	bool ParseSlotId(const char *id, const char *prefix, int count, int &index)
	{
		const size_t length = strlen(prefix);
		if (strncmp(id, prefix, length) != 0 || !id[length])
		{
			return false;
		}
		int value = 0;
		for (const char *p = id + length; *p; p++)
		{
			if (*p < '0' || *p > '9' || value >= count)
			{
				return false;
			}
			value = value * 10 + (*p - '0');
		}
		if (value >= count)
		{
			return false;
		}
		index = value;
		return true;
	}
} // namespace

int panorama_hud::ItemSlots(Layout layout)
{
	return kLayouts[static_cast<int>(layout)].items;
}

int panorama_hud::TileSlots(TileSize size)
{
	return size == TileSize::Cards ? 3 : size == TileSize::Large ? 6 : size == TileSize::Medium ? 12 : kGridSlots;
}

int panorama_hud::NavSlots(Layout layout)
{
	return kLayouts[static_cast<int>(layout)].nav;
}

std::string panorama_hud::StripColors(const std::string &text)
{
	std::string out;
	out.reserve(text.size());
	for (char c : text)
	{
		if (static_cast<unsigned char>(c) > 0x10)
		{
			out += c;
		}
	}
	return out;
}

int panorama_hud::TextCells(const std::string &text)
{
	int cells = 0;
	for (size_t i = 0; i < text.size();)
	{
		const unsigned char lead = static_cast<unsigned char>(text[i]);
		const int length = lead < 0x80 ? 1 : lead < 0xE0 ? 2 : lead < 0xF0 ? 3 : 4;
		unsigned int point = lead < 0x80 ? lead : lead & (0xFF >> (length + 1));
		for (int k = 1; k < length && i + k < text.size(); k++)
		{
			point = (point << 6) | (static_cast<unsigned char>(text[i + k]) & 0x3F);
		}
		i += length;
		const bool none = point == 0xAD || (point >= 0x200B && point <= 0x200F) || point == 0x2060 || point == 0xFEFF;
		const bool wide = (point >= 0x1100 && point <= 0x115F) || (point >= 0x2E80 && point <= 0xA4CF) || (point >= 0xAC00 && point <= 0xD7A3)
						  || (point >= 0xF900 && point <= 0xFAFF) || (point >= 0xFE30 && point <= 0xFE4F) || (point >= 0xFF00 && point <= 0xFF60)
						  || (point >= 0xFFE0 && point <= 0xFFE6) || point >= 0x1F300;
		cells += none ? 0 : wide ? 2 : 1;
	}
	return cells;
}

std::vector<panorama_hud::View::Segment> panorama_hud::SplitColors(const std::string &text, const std::string &baseColor)
{
	std::vector<View::Segment> segments;
	std::string color = baseColor;
	for (unsigned char c : text)
	{
		if (c >= 0x01 && c <= 0x10)
		{
			const char *hex = center_html::ChatCodeToHex(c);
			color = hex ? hex : baseColor;
			continue;
		}
		// Whitespace has no visible color, so it stays in the run before it.
		const bool extend = !segments.empty() && (segments.back().color == color || c == ' ' || segments.size() >= static_cast<size_t>(kRowSegments));
		if (!extend)
		{
			segments.push_back({std::string(), color});
		}
		segments.back().text += static_cast<char>(c);
	}
	return segments;
}

bool panorama_hud::Init()
{
	if (Resolved())
	{
		return true;
	}
	void *base = nullptr;
	size_t size = 0;
	if (!g_pServerGameDLL || !sig::GetModuleRange(g_pServerGameDLL, base, size))
	{
		return false;
	}
	s_createEntity = reinterpret_cast<CreateEntityByName_t>(FindSig(base, size, mmu::gamedata::kCreateEntityByNameSig, "CreateEntityByName"));
	s_dispatchSpawn = reinterpret_cast<DispatchSpawn_t>(FindSig(base, size, mmu::gamedata::kDispatchSpawnSig, "DispatchSpawn"));
	s_removeEntity = reinterpret_cast<RemoveEntity_t>(FindSig(base, size, mmu::gamedata::kRemoveEntitySig, "RemoveEntity"));
	if (!s_hud.Resolve())
	{
		MMU_LOG_WARN("CCSCustomHudLayout schema fields not found - panorama menus disabled.\n");
	}
	return Resolved();
}

bool panorama_hud::Available(Layout layout)
{
	if (!Resolved() || !g_pFullFileSystem)
	{
		return false;
	}
	// MultiAddonManager can mount the addon after load, so keep checking.
	bool &mounted = s_mounted[static_cast<int>(layout)];
	if (!mounted)
	{
		mounted = g_pFullFileSystem->FileExists(kLayouts[static_cast<int>(layout)].file);
	}
	return mounted;
}

bool panorama_hud::Show(int slot, const View &view)
{
	if (!ValidSlot(slot))
	{
		return false;
	}
	const int layout = static_cast<int>(view.layout);
	for (int other = 0; other < kMenuLayouts; other++)
	{
		if (other != layout)
		{
			HideWindow(slot, other);
		}
	}
	CCSCustomHudLayout *entity = EnsureLayout(slot, layout);
	if (!entity)
	{
		return false;
	}
	// Every class ever written stays interned, an image tile's picture is one each. Near the limit the window starts over
	// on a new entity, its classes written again from nothing.
	if (schema::Collection<CUtlString>(&FieldAt<uint8_t>(entity, s_hud.classNames), s_hud.classNamesFn).Count() > kMaxInterned - kClassHeadroom)
	{
		s_removeEntity(entity);
		s_windows[slot][layout] = Window {};
		entity = EnsureLayout(slot, layout);
		if (!entity)
		{
			return false;
		}
	}
	Window &window = s_windows[slot][layout];
	const bool cursor = !view.turning;
	if (window.capture != cursor && !SetCapture(slot, window, entity, cursor))
	{
		MMU_LOG_WARN("custom_hud_layout has no player state for slot %d.\n", slot);
		return false;
	}

	Writer w {window, entity, kLayouts[layout].prefix};
	const std::string root = w.Id("root");
	WriteMessage(w, view);
	WriteDialog(w, view);
	const std::string empty = w.Id("empty");
	w.Class(empty, "hidden", view.emptyTitle.empty());
	if (!view.emptyTitle.empty())
	{
		w.Var(w.Id("empty_title"), view.emptyTitle);
		w.Var(w.Id("empty_text"), view.emptyText);
		w.Class(empty, "loading", view.emptyLoading);
	}
	w.Class(root, "edited", !view.edited.empty());
	w.Var(w.Id("edited"), view.edited);
	const std::string scope = w.Id("scope");
	w.Class(scope, "hidden", view.scope.empty());
	if (!view.scope.empty())
	{
		w.Var(w.Id("scope_lbl"), view.scope);
		w.Class(scope, "team-t", (view.scopeTeams & 1) != 0);
		w.Class(scope, "team-ct", (view.scopeTeams & 2) != 0);
		w.Class(scope, "button", view.scopeButton);
	}
	w.Class(root, "snd", view.sounds);
	w.Swap(root, view.fontClass);
	const std::string title = w.Id("title");
	w.Var(title, view.title);
	w.Swap(title, ColorClass(view.titleColor));
	w.Class(w.Id("close"), "hidden", !view.closeButton);
	// Always there, dimmed when there's nowhere to go or nothing to refresh, so the header doesn't shift around.
	w.Class(w.Id("back"), "disabled", !view.backButton);
	w.Class(w.Id("forward"), "disabled", !view.forwardButton);
	w.Class(w.Id("refresh"), "disabled", !view.refreshButton);
	w.Class(root, "collapsed", view.collapsed);
	w.Class(w.Id("pages"), "hidden", view.nav.empty());
	// The tab row goes when nothing is in it: no tabs, no page arrows, and in the columns no chips.
	if (view.layout != Layout::List)
	{
		const bool chipsInRow = view.layout == Layout::Columns || view.layout == Layout::Table;
		w.Class(root, "notabs", view.nav.empty() && view.page.empty() && (!chipsInRow || view.chips.empty()));
	}

	const std::string navClass = ColorClass(view.navColor);
	for (int i = 0; i < kLayouts[layout].nav; i++)
	{
		const bool used = i < static_cast<int>(view.nav.size());
		const std::string panel = w.Id("nav%d", i);
		if (used)
		{
			const std::string label = w.Id("nav_lbl%d", i);
			w.Var(label, view.nav[i].label);
			w.Swap(label, navClass);
			w.Class(panel, "selected", view.nav[i].selected);
			w.Flag(panel, "marked", view.nav[i].marked);
			w.Flag(panel, "more", i == view.navMore);
		}
		w.Class(panel, "hidden", !used);
	}

	if (view.layout == Layout::Columns)
	{
		WriteColumns(w, view);
		WriteChips(w, view);
		WriteInput(w, view);
	}
	else if (view.layout == Layout::Grid)
	{
		WriteTiles(w, view);
		WriteChips(w, view);
		WriteInput(w, view);
	}
	else if (view.layout == Layout::Table)
	{
		WriteTable(w, view);
		WriteChips(w, view);
		WriteInput(w, view);
	}
	else if (view.layout == Layout::Showcase || view.layout == Layout::Studio)
	{
		WriteButtons(w, view);
		WriteChips(w, view);
		WriteInput(w, view);
		WriteInfo(w, view);
		if (view.layout == Layout::Showcase)
		{
			w.Swap(w.Id("shot"), view.image.empty() ? std::string() : "img-" + view.image);
		}
		else
		{
			w.Class(root, "turning", view.turning);
			w.Flag(root, "nohint", view.hint.empty());
			for (int i = 0; i < kHintParts; i++)
			{
				const bool used = i < static_cast<int>(view.hint.size());
				const std::string panel = w.Id("hint%d", i);
				if (used)
				{
					WriteKeys(w, view.hint[i], i, kHintKeys, "hkey%d_%d", "htxt%d");
					w.Class(panel, "caption", view.hint[i].keys.empty());
				}
				w.Class(panel, "hidden", !used);
			}
			w.Flag(root, "mirror", view.mirrored);
			// The key list: its button while there are rows, the card in the info card's place while it's open.
			const int helpRows = (std::min)(static_cast<int>(view.help.size()), kHelpRows);
			const bool helpOpen = view.helpOpen && helpRows > 0;
			w.Flag(root, "help", helpOpen);
			const std::string helpButton = w.Id("help");
			if (helpRows > 0 || w.Touched(helpButton, "hidden"))
			{
				w.Class(helpButton, "hidden", helpRows == 0);
			}
			const std::string helpCard = w.Id("helpcard");
			if (helpOpen || w.Touched(helpCard, "hidden"))
			{
				w.Class(helpCard, "hidden", !helpOpen);
			}
			if (helpOpen)
			{
				w.Var(w.Id("help_title"), view.helpTitle);
				for (int i = 0; i < kHelpRows; i++)
				{
					const bool used = i < helpRows;
					const std::string panel = w.Id("hrow%d", i);
					if (used)
					{
						WriteKeys(w, view.help[i], i, kHelpKeys, "hk%d_%d", "ht%d");
					}
					w.Class(panel, "hidden", !used);
				}
			}
			w.Class(w.Id("controls"), "hidden", view.controls.empty());
			const int controlCount = (std::min)(static_cast<int>(view.controls.size()), kStudioControls);
			const ButtonFlow controlFlow = FlowOf(view.controls, controlCount);
			for (int i = 0; i < kStudioControls; i++)
			{
				const bool used = i < controlCount;
				const std::string panel = w.Id("ctl%d", i);
				if (used)
				{
					const View::ControlButton &control = view.controls[i];
					w.Swap(panel, FillClass(controlFlow.fill[i]), "fill");
					w.Var(w.Id("ctl_lbl%d", i), control.label);
					w.Var(w.Id("ctl_sub%d", i), control.sub);
					w.Class(panel, "disabled", control.disabled);
					WriteButtonKind(w, panel, control);
				}
				w.Class(panel, "hidden", !used);
			}
			// The row holds the key list's button too, so a lone tab shows beside it.
			const bool tabs = view.controlTabs.size() > 1 || (helpRows > 0 && !view.controls.empty());
			w.Class(w.Id("ctabs"), "hidden", !tabs);
			for (int i = 0; i < kStudioControlTabs; i++)
			{
				const bool used = tabs && i < static_cast<int>(view.controlTabs.size());
				const std::string panel = w.Id("ctab%d", i);
				if (used)
				{
					w.Var(w.Id("ctab_lbl%d", i), view.controlTabs[i].label);
					w.Class(panel, "selected", view.controlTabs[i].selected);
				}
				w.Class(panel, "hidden", !used);
			}
		}
		const std::string action = w.Id("action");
		w.Class(action, "hidden", view.action.empty());
		if (!view.action.empty())
		{
			const std::string label = w.Id("action_lbl");
			w.Var(label, view.action);
			w.Swap(label, ColorClass(view.actionColor));
			w.Class(action, "disabled", view.actionDisabled);
		}
		const std::string action2 = w.Id("action2");
		if (!view.action2.empty() || w.Touched(action2, "hidden"))
		{
			w.Class(action2, "hidden", view.action2.empty());
		}
		if (!view.action2.empty())
		{
			w.Var(w.Id("action2_lbl"), view.action2);
			w.Class(action2, "disabled", view.action2Disabled);
		}
	}
	else
	{
		WriteListRows(w, view);
	}
	WritePopups(w, view);
	w.Class(root, "hidden", false);
	window.shown = true;
	return true;
}

void panorama_hud::Hide(int slot)
{
	if (!ValidSlot(slot))
	{
		return;
	}
	for (int layout = 0; layout < kMenuLayouts; layout++)
	{
		HideWindow(slot, layout);
	}
}

bool panorama_hud::IsShown(int slot)
{
	if (!ValidSlot(slot))
	{
		return false;
	}
	for (int layout = 0; layout < kMenuLayouts; layout++)
	{
		if (s_windows[slot][layout].shown && GetLayout(slot, layout))
		{
			return true;
		}
	}
	return false;
}

bool panorama_hud::ShowNotice(int slot, const Notice &notice)
{
	if (!ValidSlot(slot))
	{
		return false;
	}
	const int layout = static_cast<int>(Layout::Notice);
	CCSCustomHudLayout *entity = EnsureLayout(slot, layout);
	if (!entity)
	{
		return false;
	}
	Writer w {s_windows[slot][layout], entity, kLayouts[layout].prefix};
	const std::string root = w.Id("root");
	w.Swap(root, notice.fontClass);
	w.Var(w.Id("title"), notice.title);
	const std::pair<const char *, const std::string &> lines[] = {{"time", notice.time}, {"text", notice.text}, {"hint", notice.hint}};
	for (const auto &[suffix, value] : lines)
	{
		const std::string label = w.Id(suffix);
		w.Var(label, value);
		w.Class(label, "hidden", value.empty());
	}
	w.Class(root, "hidden", false);
	w.window.shown = true;
	return true;
}

void panorama_hud::HideNotice(int slot)
{
	if (ValidSlot(slot))
	{
		HideWindow(slot, static_cast<int>(Layout::Notice));
	}
}

bool panorama_hud::DecodeClick(const void *buf, uint32_t size, uint32_t &layoutHandle, std::string &buttonId)
{
	// CCSUsrMsg_CustomHudClicked: 1 = custom_hud_layout (varint), 2 = button_id (string).
	const uint8_t *p = static_cast<const uint8_t *>(buf);
	const uint8_t *end = p + size;
	bool haveLayout = false;
	bool haveButton = false;
	while (p && p < end)
	{
		uint64_t key = 0;
		if (!ReadVarint(p, end, key))
		{
			return false;
		}
		const uint64_t field = key >> 3;
		switch (key & 7)
		{
			case 0: // varint
			{
				uint64_t value = 0;
				if (!ReadVarint(p, end, value))
				{
					return false;
				}
				if (field == 1)
				{
					layoutHandle = static_cast<uint32_t>(value);
					haveLayout = true;
				}
				break;
			}
			case 1: // 64-bit
				if (end - p < 8)
				{
					return false;
				}
				p += 8;
				break;
			case 2: // length-delimited
			{
				uint64_t length = 0;
				if (!ReadVarint(p, end, length) || length > static_cast<uint64_t>(end - p))
				{
					return false;
				}
				if (field == 2)
				{
					buttonId.assign(reinterpret_cast<const char *>(p), static_cast<size_t>(length));
					haveButton = true;
				}
				p += static_cast<size_t>(length);
				break;
			}
			case 5: // 32-bit
				if (end - p < 4)
				{
					return false;
				}
				p += 4;
				break;
			default:
				return false;
		}
	}
	if (!haveLayout || !haveButton)
	{
		return false;
	}
	s_clicksSeen++;
	return true;
}

panorama_hud::Click panorama_hud::ParseClick(int slot, uint32_t layoutHandle, const char *buttonId, int &index)
{
	index = -1;
	if (!ValidSlot(slot) || !buttonId)
	{
		return Click::None;
	}
	int layout = 0;
	while (layout < kLayoutCount
		   && !(s_windows[slot][layout].shown && static_cast<uint32_t>(s_windows[slot][layout].entity.ToPackedInt()) == layoutHandle))
	{
		layout++;
	}
	const LayoutDef *def = layout < kLayoutCount ? &kLayouts[layout] : nullptr;
	if (!def || strncmp(buttonId, def->prefix, strlen(def->prefix)) != 0)
	{
		return Click::None;
	}
	const char *id = buttonId + strlen(def->prefix);

	struct Named
	{
		const char *id;
		Click click;
	};

	// "dlg" is the dimmed screen around the dialog's box, the box itself takes clicks without an answer.
	static constexpr Named kNamed[] = {
		{"close", Click::Close},
		{"back", Click::Back},
		{"forward", Click::Forward},
		{"refresh", Click::Refresh},
		{"collapse", Click::Collapse},
		{"action", Click::Action},
		{"action2", Click::Action2},
		{"stage", Click::Stage},
		{"help", Click::Help},
		{"step_close", Click::PopupClose},
		{"list_close", Click::PopupClose},
		{"list_prev", Click::ListPrev},
		{"list_next", Click::ListNext},
		{"prev", Click::PagePrev},
		{"next", Click::PageNext},
		{"input", Click::Input},
		{"input_x", Click::InputClear},
		{"scope", Click::Scope},
		{"dlg_yes", Click::DialogYes},
		{"dlg_no", Click::DialogNo},
		{"dlg", Click::DialogNo},
		{"notice", Click::Notice},
	};
	for (const Named &named : kNamed)
	{
		if (strcmp(id, named.id) == 0)
		{
			return named.click;
		}
	}

	struct Slotted
	{
		const char *prefix;
		int count;
		Click click;
	};

	const Slotted slotted[] = {
		{"step_b", kStepButtons, Click::Step},    {"li", kListSlots, Click::ListRow},     {"ctab", kStudioControlTabs, Click::ControlTab},
		{"ctl", kStudioControls, Click::Control}, {"nav", def->nav, Click::Nav},          {"item", def->items, Click::Item},
		{"dec", def->items, Click::ItemDec},      {"inc", def->items, Click::ItemInc},    {"corner", def->items, Click::Corner},
		{"chip", kChipSlots, Click::Chip},        {"head", kTableColumns, Click::Column},
	};
	for (const Slotted &s : slotted)
	{
		if (ParseSlotId(id, s.prefix, s.count, index))
		{
			return s.click;
		}
	}
	return Click::None;
}

void panorama_hud::OnCheckTransmit(CCheckTransmitInfo **infos, int count)
{
	// Resolved through the entity system, so a stale handle can't hide an unrelated entity reusing its index.
	int entityIndex[MAXPLAYERS][kLayoutCount];
	bool any = false;
	for (int owner = 0; owner < MAXPLAYERS; owner++)
	{
		for (int layout = 0; layout < kLayoutCount; layout++)
		{
			entityIndex[owner][layout] = GetLayout(owner, layout) ? s_windows[owner][layout].entity.GetEntryIndex() : -1;
			any = any || entityIndex[owner][layout] >= 0;
		}
	}
	if (!any)
	{
		return;
	}

	for (int i = 0; i < count; i++)
	{
		CCheckTransmitInfo *info = infos[i];
		const int recipient = *reinterpret_cast<int *>(reinterpret_cast<uintptr_t>(info) + mmu::gamedata::kCheckTransmitPlayerSlotOffset);
		for (int owner = 0; owner < MAXPLAYERS; owner++)
		{
			for (int layout = 0; layout < kLayoutCount; layout++)
			{
				const int index = entityIndex[owner][layout];
				if (index < 0)
				{
					continue;
				}
				// The engine can drop the entity from its owner's snapshot after a viewpoint change, like respawning out of spectate.
				if (owner == recipient)
				{
					info->m_pTransmitEntity->Set(index);
				}
				else
				{
					info->m_pTransmitEntity->Clear(index);
				}
			}
		}
	}
}

std::string panorama_hud::Describe()
{
	char line[256];
	std::string out;
	auto state = [](bool resolved) { return resolved ? "ok" : "MISSING"; };
	snprintf(line, sizeof(line), "signatures: CreateEntityByName %s, DispatchSpawn %s, RemoveEntity %s\n", state(s_createEntity != nullptr),
			 state(s_dispatchSpawn != nullptr), state(s_removeEntity != nullptr));
	out += line;
	snprintf(line, sizeof(line), "schema: CCSCustomHudLayout state fields %s\n", state(s_hud.Ready()));
	out += line;
	for (const LayoutDef &def : kLayouts)
	{
		const bool mounted = g_pFullFileSystem && g_pFullFileSystem->FileExists(def.file);
		snprintf(line, sizeof(line), "layout %s: %s %s\n", def.name, def.file, mounted ? "mounted" : "NOT MOUNTED");
		out += line;
	}
	snprintf(line, sizeof(line), "clicks seen: %d\n", s_clicksSeen);
	out += line;

	int windows = 0;
	for (int slot = 0; slot < MAXPLAYERS; slot++)
	{
		for (int layout = 0; layout < kLayoutCount; layout++)
		{
			const Window &window = s_windows[slot][layout];
			if (!window.entity.IsValid())
			{
				continue;
			}
			windows++;
			const bool alive = GetLayout(slot, layout) != nullptr;
			snprintf(line, sizeof(line), "slot %d %s: entity %d %s, %s, capture %s, %d classes, %d vars\n", slot, kLayouts[layout].name,
					 window.entity.GetEntryIndex(), alive ? "live" : "GONE", window.shown ? "shown" : "hidden", window.capture ? "on" : "off",
					 static_cast<int>(window.classes.size()), static_cast<int>(window.vars.size()));
			out += line;
		}
	}
	if (windows == 0)
	{
		out += "no windows spawned\n";
	}
	return out;
}

void panorama_hud::OnClientDisconnect(int slot)
{
	if (!ValidSlot(slot))
	{
		return;
	}
	for (int layout = 0; layout < kLayoutCount; layout++)
	{
		if (CCSCustomHudLayout *entity = GetLayout(slot, layout))
		{
			s_removeEntity(entity);
		}
		s_windows[slot][layout] = Window {};
	}
}

void panorama_hud::OnLevelShutdown()
{
	for (auto &windows : s_windows)
	{
		for (Window &window : windows)
		{
			window = Window {};
		}
	}
	// The next map may mount different addons.
	for (bool &mounted : s_mounted)
	{
		mounted = false;
	}
}

void panorama_hud::RemoveOrphans()
{
	if (!s_removeEntity || !g_pEntitySystem)
	{
		return;
	}
	std::vector<CEntityInstance *> orphans;
	EntityInstanceByClassIter_t iter("custom_hud_layout");
	for (CEntityInstance *entity = iter.First(); entity; entity = iter.Next())
	{
		const char *name = entity->m_pEntity ? entity->m_pEntity->m_name.String() : nullptr;
		if (name && strncmp(name, kTargetPrefix, strlen(kTargetPrefix)) == 0)
		{
			orphans.push_back(entity);
		}
	}
	for (CEntityInstance *entity : orphans)
	{
		s_removeEntity(entity);
	}
}

void panorama_hud::Shutdown()
{
	for (int slot = 0; slot < MAXPLAYERS; slot++)
	{
		for (int layout = 0; layout < kLayoutCount; layout++)
		{
			Window &window = s_windows[slot][layout];
			if (CCSCustomHudLayout *entity = GetLayout(slot, layout))
			{
				if (window.capture)
				{
					SetCapture(slot, window, entity, false);
				}
				s_removeEntity(entity);
			}
			window = Window {};
		}
	}
}
