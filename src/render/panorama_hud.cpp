#include "panorama_hud.h"
#include "src/common.h"
#include "src/entity/ccscustomhudlayout.h"
#include "src/render/center_html.h"
#include "mmu/gamedata.h"
#include "mmu/log.h"
#include "mmu/sigscan.h"

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
	constexpr LayoutDef kLayouts[kLayoutCount] = {
		{"panorama/layout/custom_game/cs2menus/menu.vxml_c", "cm_", "list", panorama_hud::kItemSlots, panorama_hud::kNavSlots},
		{"panorama/layout/custom_game/cs2menus/grid.vxml_c", "cg_", "grid", panorama_hud::kGridSlots, panorama_hud::kGridTabs},
		{"panorama/layout/custom_game/cs2menus/showcase.vxml_c", "cx_", "showcase", panorama_hud::kShowcaseSlots, panorama_hud::kShowcaseTabs},
	};
	// Lets a reloaded plugin find the windows it left behind.
	constexpr const char *kTargetPrefix = "cs2menus_";

	using CreateEntityByName_t = CEntityInstance *(*)(const char *className, int forcedIndex);
	using DispatchSpawn_t = void (*)(CEntityInstance *entity, CEntityKeyValues *keyValues);
	using RemoveEntity_t = void (*)(CEntityInstance *entity);
	using SetHasClass_t = void (*)(CCSCustomHudLayout *layout, CUtlString *panelId, CUtlString *className, uint32_t status);
	using SetDialogVariableString_t = void (*)(CCSCustomHudLayout *layout, CUtlString *panelId, CUtlString *name, CUtlString *value);
	using SetInputCaptureEnabled_t = void (*)(CCSCustomHudLayout *layout, int slot, bool enabled);

	CreateEntityByName_t s_createEntity = nullptr;
	DispatchSpawn_t s_dispatchSpawn = nullptr;
	RemoveEntity_t s_removeEntity = nullptr;
	SetHasClass_t s_setHasClass = nullptr;
	SetDialogVariableString_t s_setDialogVariableString = nullptr;
	SetInputCaptureEnabled_t s_setInputCaptureEnabled = nullptr;

	bool s_mounted[kLayoutCount] = {};
	// Clicks decoded from any layout, so the diagnostic can tell a dead click hook from an unclicked menu.
	int s_clicksSeen = 0;

	bool Resolved()
	{
		return s_createEntity && s_dispatchSpawn && s_removeEntity && s_setHasClass && s_setDialogVariableString && s_setInputCaptureEnabled;
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
		// Entries start stamped with slot 0 and the game's setter doesn't restamp them.
		CPlayerSlot &stamped = *reinterpret_cast<CPlayerSlot *>(reinterpret_cast<uintptr_t>(state) + slotOffset);
		if (stamped.Get() != slot)
		{
			stamped = CPlayerSlot(slot);
			state->MarkChanged();
		}
		s_setInputCaptureEnabled(layout, slot, enabled);
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

		// The game interns the names and flags the network change itself.
		void Class(const std::string &panel, const char *cls, bool present)
		{
			const std::string key = panel + ' ' + cls;
			auto it = window.classes.find(key);
			if (it != window.classes.end() && it->second == present)
			{
				return;
			}
			CUtlString panelId(panel.c_str());
			CUtlString className(cls);
			s_setHasClass(entity, &panelId, &className, present ? 1 : 0);
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
			CUtlString panelId(panel.c_str());
			CUtlString name(panel.c_str());
			CUtlString text(value.c_str());
			s_setDialogVariableString(entity, &panelId, &name, &text);
			window.vars[key] = value;
		}

		// Replaces the panel's previous class from the same set. Empty removes it.
		void Swap(const std::string &panel, const std::string &cls)
		{
			std::string &current = window.swaps[panel];
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

	// Grid tiles, or the showcase's buttons, which have no image of their own.
	void WriteTiles(Writer &w, const panorama_hud::View &view, int slots, bool images)
	{
		if (images)
		{
			w.Swap(w.Id("tiles"), view.tiles == panorama_hud::TileSize::Cards    ? "size-xl"
								  : view.tiles == panorama_hud::TileSize::Large  ? "size-l"
								  : view.tiles == panorama_hud::TileSize::Medium ? "size-m"
																				 : "");
		}
		for (int i = 0; i < slots; i++)
		{
			const bool used = i < static_cast<int>(view.rows.size());
			const std::string panel = w.Id("item%d", i);
			if (used)
			{
				const panorama_hud::View::Row &row = view.rows[i];
				// A tile's name is one label, in the color its text starts with.
				std::string name;
				for (const panorama_hud::View::Segment &segment : row.segments)
				{
					name += segment.text;
				}
				const char *color = ColorClass(row.segments.empty() ? std::string() : row.segments[0].color);
				const std::string nameLabel = w.Id("name%d", i);
				w.Var(nameLabel, name);
				w.Swap(nameLabel, color);
				const std::string value = w.Id("val%d", i);
				w.Var(value, row.value);
				w.Swap(value, color);
				if (images)
				{
					w.Swap(w.Id("img%d", i), row.image.empty() ? std::string() : "img-" + row.image);
				}
				w.Control(panel, row);
			}
			w.Class(panel, "hidden", !used);
		}
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

	void WritePopups(Writer &w, const panorama_hud::View &view)
	{
		const bool popup = view.step.open || view.list.open;
		w.Class(w.Id("root"), "shift", popup);
		// The showcase draws the image inside, the others beside the box.
		if (view.layout != Layout::Showcase)
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
					w.Var(w.Id("li_lbl%d", i), view.list.rows[i].label);
					w.Class(panel, "selected", view.list.rows[i].selected);
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
	s_setHasClass = reinterpret_cast<SetHasClass_t>(FindSig(base, size, mmu::gamedata::kCustomHudSetHasClassSig, "CCSCustomHudLayout::SetHasClass"));
	s_setDialogVariableString = reinterpret_cast<SetDialogVariableString_t>(
		FindSig(base, size, mmu::gamedata::kCustomHudSetDialogVariableStringSig, "CCSCustomHudLayout::SetDialogVariableString"));
	s_setInputCaptureEnabled = reinterpret_cast<SetInputCaptureEnabled_t>(
		FindSig(base, size, mmu::gamedata::kCustomHudSetInputCaptureEnabledSig, "CCSCustomHudLayout::SetInputCaptureEnabled"));
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
	for (int other = 0; other < kLayoutCount; other++)
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
	Window &window = s_windows[slot][layout];
	if (!window.capture && !SetCapture(slot, window, entity, true))
	{
		MMU_LOG_WARN("custom_hud_layout has no player state for slot %d.\n", slot);
		return false;
	}

	Writer w {window, entity, kLayouts[layout].prefix};
	const std::string root = w.Id("root");
	w.Class(root, "snd", view.sounds);
	w.Swap(root, view.fontClass);
	const std::string title = w.Id("title");
	w.Var(title, view.title);
	w.Swap(title, ColorClass(view.titleColor));
	w.Class(w.Id("close"), "hidden", !view.closeButton);
	// Always there, dimmed when there's nowhere to go, so the header doesn't shift around.
	w.Class(w.Id("back"), "disabled", !view.backButton);
	w.Class(w.Id("forward"), "disabled", !view.forwardButton);
	w.Class(w.Id("refresh"), "hidden", !view.refreshButton);
	w.Class(root, "collapsed", view.collapsed);
	w.Class(w.Id("pages"), "hidden", view.nav.empty());

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
		}
		w.Class(panel, "hidden", !used);
	}

	if (view.layout == Layout::Grid)
	{
		WriteTiles(w, view, kGridSlots, true);
	}
	else if (view.layout == Layout::Showcase)
	{
		WriteTiles(w, view, kShowcaseSlots, false);
		w.Swap(w.Id("shot"), view.image.empty() ? std::string() : "img-" + view.image);
		const std::string action = w.Id("action");
		w.Class(action, "hidden", view.action.empty());
		if (!view.action.empty())
		{
			const std::string label = w.Id("action_lbl");
			w.Var(label, view.action);
			w.Swap(label, ColorClass(view.actionColor));
			w.Class(action, "disabled", view.actionDisabled);
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
	for (int layout = 0; layout < kLayoutCount; layout++)
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
	for (int layout = 0; layout < kLayoutCount; layout++)
	{
		if (s_windows[slot][layout].shown && GetLayout(slot, layout))
		{
			return true;
		}
	}
	return false;
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
	if (strcmp(id, "close") == 0)
	{
		return Click::Close;
	}
	if (strcmp(id, "back") == 0)
	{
		return Click::Back;
	}
	if (strcmp(id, "forward") == 0)
	{
		return Click::Forward;
	}
	if (strcmp(id, "refresh") == 0)
	{
		return Click::Refresh;
	}
	if (strcmp(id, "collapse") == 0)
	{
		return Click::Collapse;
	}
	if (strcmp(id, "action") == 0)
	{
		return Click::Action;
	}
	if (strcmp(id, "step_close") == 0 || strcmp(id, "list_close") == 0)
	{
		return Click::PopupClose;
	}
	if (strcmp(id, "list_prev") == 0)
	{
		return Click::ListPrev;
	}
	if (strcmp(id, "list_next") == 0)
	{
		return Click::ListNext;
	}
	if (strcmp(id, "prev") == 0)
	{
		return Click::PagePrev;
	}
	if (strcmp(id, "next") == 0)
	{
		return Click::PageNext;
	}
	if (ParseSlotId(id, "step_b", kStepButtons, index))
	{
		return Click::Step;
	}
	if (ParseSlotId(id, "li", kListSlots, index))
	{
		return Click::ListRow;
	}
	if (ParseSlotId(id, "nav", def->nav, index))
	{
		return Click::Nav;
	}
	if (ParseSlotId(id, "item", def->items, index))
	{
		return Click::Item;
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
	snprintf(line, sizeof(line), "signatures: SetHasClass %s, SetDialogVariableString %s, SetInputCaptureEnabled %s\n",
			 state(s_setHasClass != nullptr), state(s_setDialogVariableString != nullptr), state(s_setInputCaptureEnabled != nullptr));
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
