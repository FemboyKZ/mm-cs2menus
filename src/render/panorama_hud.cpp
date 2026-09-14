#include "panorama_hud.h"
#include "src/common.h"
#include "src/entity/ccscustomhudlayout.h"
#include "mmu/gamedata.h"
#include "mmu/log.h"
#include "mmu/sigscan.h"

#include <checktransmitinfo.h>
#include <entity2/entitykeyvalues.h>
#include <entity2/entitysystem.h>
#include <filesystem.h>

#include <cstdio>
#include <cstring>
#include <unordered_map>

namespace
{
	constexpr const char *kLayout = "panorama/layout/custom_game/cs2menus/menu.vxml_c";
	// Lets a reloaded plugin find the windows it left behind.
	constexpr const char *kTargetPrefix = "cs2menus_";
	// The game refuses to intern past this per table.
	constexpr int kMaxInterned = 1024;

	using CreateEntityByName_t = CEntityInstance *(*)(const char *className, int forcedIndex);
	using DispatchSpawn_t = void (*)(CEntityInstance *entity, CEntityKeyValues *keyValues);
	using RemoveEntity_t = void (*)(CEntityInstance *entity);

	CreateEntityByName_t s_createEntity = nullptr;
	DispatchSpawn_t s_dispatchSpawn = nullptr;
	RemoveEntity_t s_removeEntity = nullptr;
	bool s_mounted = false;
	// Clicks decoded from any layout, so the diagnostic can tell a dead click hook from an unclicked menu.
	int s_clicksSeen = 0;

	struct Window
	{
		CEntityHandle entity;
		bool shown = false;
		bool capture = false;
		std::string font;
		// Last written values, keyed "panel class" and "panel var", so unchanged writes are skipped.
		std::unordered_map<std::string, bool> classes;
		std::unordered_map<std::string, std::string> vars;
	};

	Window s_windows[MAXPLAYERS];

	template<typename T>
	T &Member(void *object, int32_t offset)
	{
		return *reinterpret_cast<T *>(reinterpret_cast<uintptr_t>(object) + offset);
	}

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

	CCSCustomHudLayout *GetLayout(int slot)
	{
		const CEntityHandle &handle = s_windows[slot].entity;
		if (!handle.IsValid() || !g_pEntitySystem)
		{
			return nullptr;
		}
		return static_cast<CCSCustomHudLayout *>(g_pEntitySystem->GetEntityInstance(handle));
	}

	CCSCustomHudLayout *EnsureLayout(int slot)
	{
		if (CCSCustomHudLayout *layout = GetLayout(slot))
		{
			return layout;
		}
		// The caches belonged to an entity that's gone.
		s_windows[slot] = Window {};
		if (!g_pEntitySystem || !panorama_hud::Available())
		{
			return nullptr;
		}
		CEntityInstance *entity = s_createEntity("custom_hud_layout", -1);
		if (!entity)
		{
			return nullptr;
		}
		char name[32];
		snprintf(name, sizeof(name), "%s%d", kTargetPrefix, slot);
		CEntityKeyValues *keyValues = new CEntityKeyValues();
		keyValues->SetString("layout", kLayout);
		keyValues->SetString("targetname", name);
		s_dispatchSpawn(entity, keyValues);
		s_windows[slot].entity = entity->GetRefEHandle();
		return static_cast<CCSCustomHudLayout *>(entity);
	}

	int Intern(CCSCustomHudLayout *layout, const schema::Collection<CUtlString> &table, const char *str)
	{
		if (!table.IsValid())
		{
			return -1;
		}
		const int count = table.Count();
		for (int i = 0; i < count; i++)
		{
			const char *existing = table.At(i)->Get();
			if (existing && strcmp(existing, str) == 0)
			{
				return i;
			}
		}
		CUtlString *added = count < kMaxInterned ? table.Append() : nullptr;
		if (!added)
		{
			MMU_LOG_WARN("custom_hud_layout: could not intern \"%s\".\n", str);
			return -1;
		}
		*added = str;
		layout->NetworkStateChanged(NetworkStateChangedData(true));
		return count;
	}

	bool SetStateClass(CCSCustomHudLayoutState *state, uint16_t panel, uint16_t cls, bool present)
	{
		const int32_t panelOffset = HUDPanelHasClass_t::m_nPanelIdIndex_Offset();
		const int32_t classOffset = HUDPanelHasClass_t::m_nClassNameIndex_Offset();
		const int32_t statusOffset = HUDPanelHasClass_t::m_eClassStatus_Offset();
		schema::Collection<HUDPanelHasClass_t> entries = state->m_vecHasClasses();
		if (panelOffset < 0 || classOffset < 0 || statusOffset < 0 || !entries.IsValid())
		{
			return false;
		}

		HUDPanelHasClass_t *entry = nullptr;
		const int count = entries.Count();
		for (int i = 0; i < count && !entry; i++)
		{
			HUDPanelHasClass_t *candidate = entries.At(i);
			if (Member<uint16_t>(candidate, panelOffset) == panel && Member<uint16_t>(candidate, classOffset) == cls)
			{
				entry = candidate;
			}
		}
		if (!entry)
		{
			entry = entries.Append();
			if (!entry)
			{
				return false;
			}
			Member<uint16_t>(entry, panelOffset) = panel;
			Member<uint16_t>(entry, classOffset) = cls;
		}
		Member<uint32_t>(entry, statusOffset) = present ? 1 : 0;
		state->MarkChanged();
		return true;
	}

	bool SetStateVar(CCSCustomHudLayoutState *state, uint16_t panel, uint16_t var, const char *value)
	{
		const int32_t panelOffset = HUDPanelDialogVariableString_t::m_nPanelIdIndex_Offset();
		const int32_t varOffset = HUDPanelDialogVariableString_t::m_nDialogVariableIndex_Offset();
		const int32_t valueOffset = HUDPanelDialogVariableString_t::m_sValue_Offset();
		const int32_t setOffset = HUDPanelDialogVariableString_t::m_bIsSet_Offset();
		schema::Collection<HUDPanelDialogVariableString_t> entries = state->m_vecDialogVariableStrings();
		if (panelOffset < 0 || varOffset < 0 || valueOffset < 0 || setOffset < 0 || !entries.IsValid())
		{
			return false;
		}

		HUDPanelDialogVariableString_t *entry = nullptr;
		const int count = entries.Count();
		for (int i = 0; i < count && !entry; i++)
		{
			HUDPanelDialogVariableString_t *candidate = entries.At(i);
			if (Member<uint16_t>(candidate, panelOffset) == panel && Member<uint16_t>(candidate, varOffset) == var)
			{
				entry = candidate;
			}
		}
		if (!entry)
		{
			entry = entries.Append();
			if (!entry)
			{
				return false;
			}
			Member<uint16_t>(entry, panelOffset) = panel;
			Member<uint16_t>(entry, varOffset) = var;
		}
		Member<CUtlString>(entry, valueOffset) = value;
		Member<bool>(entry, setOffset) = true;
		state->MarkChanged();
		return true;
	}

	void WriteClass(int slot, CCSCustomHudLayout *layout, const char *panel, const char *cls, bool present)
	{
		Window &window = s_windows[slot];
		const std::string key = std::string(panel) + ' ' + cls;
		auto it = window.classes.find(key);
		if (it != window.classes.end() && it->second == present)
		{
			return;
		}
		CCSCustomHudLayoutState *state = layout->GlobalState();
		const int panelIndex = Intern(layout, layout->m_vecPanelIds(), panel);
		const int classIndex = Intern(layout, layout->m_vecClassNames(), cls);
		if (state && panelIndex >= 0 && classIndex >= 0
			&& SetStateClass(state, static_cast<uint16_t>(panelIndex), static_cast<uint16_t>(classIndex), present))
		{
			window.classes[key] = present;
		}
	}

	void WriteVar(int slot, CCSCustomHudLayout *layout, const char *panel, const char *var, const std::string &value)
	{
		Window &window = s_windows[slot];
		const std::string key = std::string(panel) + ' ' + var;
		auto it = window.vars.find(key);
		if (it != window.vars.end() && it->second == value)
		{
			return;
		}
		CCSCustomHudLayoutState *state = layout->GlobalState();
		const int panelIndex = Intern(layout, layout->m_vecPanelIds(), panel);
		const int varIndex = Intern(layout, layout->m_vecDialogVariableNames(), var);
		if (state && panelIndex >= 0 && varIndex >= 0
			&& SetStateVar(state, static_cast<uint16_t>(panelIndex), static_cast<uint16_t>(varIndex), value.c_str()))
		{
			window.vars[key] = value;
		}
	}

	bool SetCapture(int slot, CCSCustomHudLayout *layout, bool enabled)
	{
		schema::Collection<CCSCustomHudLayoutState> states = layout->m_vecPlayerLayoutStates();
		const int16_t captureOffset = CCSCustomHudLayoutState::m_bInputCaptureEnabled_Offset();
		const int16_t slotOffset = CCSCustomHudLayoutState::m_playerSlot_Offset();
		if (slot >= states.Count() || captureOffset <= 0 || slotOffset <= 0)
		{
			return false;
		}
		CCSCustomHudLayoutState *state = states.At(slot);
		if (!state)
		{
			return false;
		}
		// Entries start stamped with slot 0, and capture applies to the stamped slot.
		Member<CPlayerSlot>(state, slotOffset) = CPlayerSlot(slot);
		Member<bool>(state, captureOffset) = enabled;
		state->MarkChanged();
		s_windows[slot].capture = enabled;
		return true;
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

bool panorama_hud::Init()
{
	if (s_createEntity && s_dispatchSpawn && s_removeEntity)
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
	return s_createEntity && s_dispatchSpawn && s_removeEntity;
}

bool panorama_hud::Available()
{
	if (!s_createEntity || !s_dispatchSpawn || !s_removeEntity || !g_pFullFileSystem)
	{
		return false;
	}
	// MultiAddonManager can mount the addon after load, so keep checking.
	if (!s_mounted)
	{
		s_mounted = g_pFullFileSystem->FileExists(kLayout);
	}
	return s_mounted;
}

bool panorama_hud::Show(int slot, const View &view)
{
	if (!ValidSlot(slot))
	{
		return false;
	}
	CCSCustomHudLayout *layout = EnsureLayout(slot);
	if (!layout)
	{
		return false;
	}
	Window &window = s_windows[slot];
	if (!window.capture && !SetCapture(slot, layout, true))
	{
		MMU_LOG_WARN("custom_hud_layout has no player state for slot %d.\n", slot);
		return false;
	}

	WriteClass(slot, layout, "cm_root", "snd", view.sounds);
	if (window.font != view.fontClass)
	{
		if (!window.font.empty())
		{
			WriteClass(slot, layout, "cm_root", window.font.c_str(), false);
		}
		window.font = view.fontClass;
	}
	if (!view.fontClass.empty())
	{
		WriteClass(slot, layout, "cm_root", view.fontClass.c_str(), true);
	}
	WriteVar(slot, layout, "cm_title", "cm_title", view.title);
	WriteClass(slot, layout, "cm_close", "hidden", !view.closeButton);
	WriteClass(slot, layout, "cm_pages", "hidden", view.nav.empty());

	char panel[24];
	char label[24];
	char var[24];
	for (int i = 0; i < kNavSlots; i++)
	{
		const bool used = i < static_cast<int>(view.nav.size());
		snprintf(panel, sizeof(panel), "cm_nav%d", i);
		if (used)
		{
			snprintf(label, sizeof(label), "cm_nav_lbl%d", i);
			snprintf(var, sizeof(var), "cm_nl%d", i);
			WriteVar(slot, layout, label, var, view.nav[i].label);
			WriteClass(slot, layout, panel, "selected", view.nav[i].selected);
		}
		WriteClass(slot, layout, panel, "hidden", !used);
	}
	for (int i = 0; i < kItemSlots; i++)
	{
		const bool used = i < static_cast<int>(view.rows.size());
		snprintf(panel, sizeof(panel), "cm_item%d", i);
		if (used)
		{
			const View::Row &row = view.rows[i];
			snprintf(label, sizeof(label), "cm_item_lbl%d", i);
			snprintf(var, sizeof(var), "cm_il%d", i);
			WriteVar(slot, layout, label, var, row.text);
			snprintf(label, sizeof(label), "cm_item_val%d", i);
			snprintf(var, sizeof(var), "cm_iv%d", i);
			WriteVar(slot, layout, label, var, row.value);
			WriteClass(slot, layout, panel, "disabled", row.disabled);
		}
		WriteClass(slot, layout, panel, "hidden", !used);
	}
	WriteClass(slot, layout, "cm_root", "hidden", false);
	window.shown = true;
	return true;
}

void panorama_hud::Hide(int slot)
{
	if (!ValidSlot(slot) || !s_windows[slot].shown)
	{
		return;
	}
	s_windows[slot].shown = false;
	if (CCSCustomHudLayout *layout = GetLayout(slot))
	{
		WriteClass(slot, layout, "cm_root", "hidden", true);
		SetCapture(slot, layout, false);
	}
}

bool panorama_hud::IsShown(int slot)
{
	return ValidSlot(slot) && s_windows[slot].shown && GetLayout(slot);
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
	if (!ValidSlot(slot) || !buttonId || !s_windows[slot].shown || static_cast<uint32_t>(s_windows[slot].entity.ToPackedInt()) != layoutHandle)
	{
		return Click::None;
	}
	if (strcmp(buttonId, "cm_close") == 0)
	{
		return Click::Close;
	}
	if (ParseSlotId(buttonId, "cm_nav", kNavSlots, index))
	{
		return Click::Nav;
	}
	if (ParseSlotId(buttonId, "cm_item", kItemSlots, index))
	{
		return Click::Item;
	}
	return Click::None;
}

void panorama_hud::OnCheckTransmit(CCheckTransmitInfo **infos, int count)
{
	// Resolved through the entity system, so a stale handle can't hide an unrelated entity reusing its index.
	int entityIndex[MAXPLAYERS];
	bool any = false;
	for (int owner = 0; owner < MAXPLAYERS; owner++)
	{
		entityIndex[owner] = GetLayout(owner) ? s_windows[owner].entity.GetEntryIndex() : -1;
		any = any || entityIndex[owner] >= 0;
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
			if (entityIndex[owner] < 0)
			{
				continue;
			}
			// The engine can drop the entity from its owner's snapshot after a viewpoint change, like respawning out of spectate.
			if (owner == recipient)
			{
				info->m_pTransmitEntity->Set(entityIndex[owner]);
			}
			else
			{
				info->m_pTransmitEntity->Clear(entityIndex[owner]);
			}
		}
	}
}

std::string panorama_hud::Describe()
{
	char line[256];
	std::string out;
	snprintf(line, sizeof(line), "signatures: CreateEntityByName %s, DispatchSpawn %s, RemoveEntity %s\n", s_createEntity ? "ok" : "MISSING",
			 s_dispatchSpawn ? "ok" : "MISSING", s_removeEntity ? "ok" : "MISSING");
	out += line;
	const bool mounted = g_pFullFileSystem && g_pFullFileSystem->FileExists(kLayout);
	snprintf(line, sizeof(line), "layout: %s %s\n", kLayout, mounted ? "mounted" : "NOT MOUNTED");
	out += line;
	snprintf(line, sizeof(line), "clicks seen: %d\n", s_clicksSeen);
	out += line;

	int windows = 0;
	for (int slot = 0; slot < MAXPLAYERS; slot++)
	{
		const Window &window = s_windows[slot];
		if (!window.entity.IsValid())
		{
			continue;
		}
		windows++;
		const bool alive = GetLayout(slot) != nullptr;
		snprintf(line, sizeof(line), "slot %d: entity %d %s, %s, capture %s, %d classes, %d vars\n", slot, window.entity.GetEntryIndex(),
				 alive ? "live" : "GONE", window.shown ? "shown" : "hidden", window.capture ? "on" : "off", static_cast<int>(window.classes.size()),
				 static_cast<int>(window.vars.size()));
		out += line;
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
	if (CCSCustomHudLayout *layout = GetLayout(slot))
	{
		s_removeEntity(layout);
	}
	s_windows[slot] = Window {};
}

void panorama_hud::OnLevelShutdown()
{
	for (Window &window : s_windows)
	{
		window = Window {};
	}
	// The next map may mount different addons.
	s_mounted = false;
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
		if (CCSCustomHudLayout *layout = GetLayout(slot))
		{
			if (s_windows[slot].capture)
			{
				SetCapture(slot, layout, false);
			}
			s_removeEntity(layout);
		}
		s_windows[slot] = Window {};
	}
}
