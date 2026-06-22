#include "menu_manager.h"
#include "src/entity/in_buttons.h"
#include "src/render/center_html.h"
#include "src/utils/print_utils.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>

// Sentinel stored in a per-menu override to mean "explicitly disabled" (MenuButton::None),
// as opposed to mask 0 which means "no override - inherit the server config binding".
// All bits set is never a real single-button IN_* mask.
static constexpr uint64_t kNavDisabledSentinel = ~0ull;

// Map a public MenuButton to its IN_* mask + footer label.
// Default -> {0, ""} (inherit); None -> {sentinel, ""} (disabled).
static void MenuButtonToBinding(MenuButton button, uint64_t &outMask, const char *&outLabel)
{
	switch (button)
	{
		case MenuButton::W:
			outMask = in_button::Forward;
			outLabel = "W";
			return;
		case MenuButton::A:
			outMask = in_button::MoveLeft;
			outLabel = "A";
			return;
		case MenuButton::S:
			outMask = in_button::Back;
			outLabel = "S";
			return;
		case MenuButton::D:
			outMask = in_button::MoveRight;
			outLabel = "D";
			return;
		case MenuButton::Use:
			outMask = in_button::Use;
			outLabel = "E";
			return;
		case MenuButton::Speed:
			outMask = in_button::Speed;
			outLabel = "SHIFT";
			return;
		case MenuButton::Duck:
			outMask = in_button::Duck;
			outLabel = "CTRL";
			return;
		case MenuButton::Jump:
			outMask = in_button::Jump;
			outLabel = "SPACE";
			return;
		case MenuButton::Reload:
			outMask = in_button::Reload;
			outLabel = "R";
			return;
		case MenuButton::Attack:
			outMask = in_button::Attack;
			outLabel = "MOUSE1";
			return;
		case MenuButton::Attack2:
			outMask = in_button::Attack2;
			outLabel = "MOUSE2";
			return;
		case MenuButton::Score:
			outMask = in_button::Score;
			outLabel = "TAB";
			return;
		case MenuButton::Inspect:
			outMask = in_button::Inspect;
			outLabel = "F";
			return;
		case MenuButton::None:
			outMask = kNavDisabledSentinel;
			outLabel = "";
			return;
		case MenuButton::Default:
		default:
			outMask = 0;
			outLabel = "";
			return;
	}
}

MenuManager g_MenuManager;

// HTML menus decay, we re-send with this duration and refresh a bit faster.
static constexpr int kHtmlDurationSecs = 3;
static constexpr float kHtmlRefreshInterval = 1.0f;
static const char *kHtmlMarker = "\xE2\x96\xB6 "; // ▶

// Cap on nested menu callbacks, so a consumer that re-displays a menu inside its
// own onSelect/onEnd can't recurse the server into a stack overflow.
static constexpr int kMaxCallbackDepth = 16;

namespace
{
	struct DepthGuard
	{
		int &depth;
		bool entered = false;

		explicit DepthGuard(int &d) : depth(d) {}

		bool enter()
		{
			if (depth >= kMaxCallbackDepth)
			{
				return false;
			}
			depth++;
			entered = true;
			return true;
		}

		~DepthGuard()
		{
			if (entered)
			{
				depth--;
			}
		}
	};
} // namespace

static bool IsNumericInput(const char *text, const std::string &prefixes, int &outNum)
{
	const char *p = text;
	while (*p == ' ' || *p == '\t')
	{
		p++;
	}
	if (!*p)
	{
		return false;
	}

	if (*p && prefixes.find(*p) != std::string::npos)
	{
		p++;
	}

	if (!isdigit(static_cast<unsigned char>(*p)))
	{
		return false;
	}

	char *end = nullptr;
	long v = strtol(p, &end, 10);
	if (end == p)
	{
		return false;
	}

	while (*end == ' ' || *end == '\t')
	{
		end++;
	}
	if (*end != '\0')
	{
		return false;
	}

	outNum = static_cast<int>(v);
	return true;
}

MenuManager::MenuDef *MenuManager::Find(MenuHandle menu)
{
	auto it = m_menus.find(menu);
	return it == m_menus.end() ? nullptr : &it->second;
}

const MenuManager::MenuDef *MenuManager::Find(MenuHandle menu) const
{
	auto it = m_menus.find(menu);
	return it == m_menus.end() ? nullptr : &it->second;
}

MenuHandle MenuManager::CreateMenu(MenuType type, const char *title, MenuItemSelectFn onSelect)
{
	MenuHandle h = m_nextHandle++;
	MenuDef def;
	def.type = (type == MenuType::Default) ? m_settings.defaultType : type;
	// If HTML rendering or input is unavailable, fall back to chat menus.
	if (def.type != MenuType::Chat && !m_htmlAvailable)
	{
		def.type = MenuType::Chat;
	}
	def.title = title ? title : "";
	def.onSelect = std::move(onSelect);
	def.exitButton = m_settings.defaultExitButton;
	def.exitItem = m_settings.defaultExitItem;
	m_menus.emplace(h, std::move(def));
	return h;
}

int MenuManager::AddItem(MenuHandle menu, const char *text, const char *info, bool disabled)
{
	MenuDef *def = Find(menu);
	if (!def)
	{
		return -1;
	}
	def->items.push_back({text ? text : "", info ? info : "", disabled});
	return static_cast<int>(def->items.size()) - 1;
}

void MenuManager::SetTitle(MenuHandle menu, const char *title)
{
	if (MenuDef *def = Find(menu))
	{
		def->title = title ? title : "";
	}
}

void MenuManager::SetExitButton(MenuHandle menu, bool enabled)
{
	if (MenuDef *def = Find(menu))
	{
		def->exitButton = enabled;
	}
}

void MenuManager::SetCloseOnSelect(MenuHandle menu, bool enabled)
{
	if (MenuDef *def = Find(menu))
	{
		def->closeOnSelect = enabled;
	}
}

void MenuManager::SetMenuEndCallback(MenuHandle menu, MenuEndFn onEnd)
{
	if (MenuDef *def = Find(menu))
	{
		def->onEnd = std::move(onEnd);
	}
}

void MenuManager::SetMenuKey(MenuHandle menu, MenuNavAction action, MenuButton button)
{
	MenuDef *def = Find(menu);
	int idx = static_cast<int>(action);
	if (!def || idx < 0 || idx > 3)
	{
		return;
	}

	uint64_t mask = 0;
	const char *label = "";
	MenuButtonToBinding(button, mask, label);

	def->navOverride[idx].mask = mask; // 0 (MenuButton::Default) clears the override
	def->navOverride[idx].label = label;
}

void MenuManager::SetExitItem(MenuHandle menu, bool enabled)
{
	if (MenuDef *def = Find(menu))
	{
		def->exitItem = enabled;
	}
}

bool MenuManager::HtmlShowsExitRow(const MenuDef &def) const
{
	// Must be exitable, and either explicitly requested or forced because the Back
	// key is disabled (otherwise the player would have no way out but the timeout).
	return def.exitButton && (def.exitItem || EffectiveNavMask(def, 3) == 0);
}

int MenuManager::HtmlRowCount(const MenuDef &def) const
{
	return static_cast<int>(def.items.size()) + (HtmlShowsExitRow(def) ? 1 : 0);
}

uint64_t MenuManager::EffectiveNavMask(const MenuDef &def, int action) const
{
	uint64_t override_ = def.navOverride[action].mask;
	if (override_ == kNavDisabledSentinel)
	{
		return 0; // explicitly disabled for this menu (MenuButton::None)
	}
	if (override_ != 0)
	{
		return override_;
	}
	switch (action)
	{
		case 0:
			return m_settings.keyUp;
		case 1:
			return m_settings.keyDown;
		case 2:
			return m_settings.keySelect;
		default:
			return m_settings.keyBack;
	}
}

std::string MenuManager::EffectiveNavLabel(const MenuDef &def, int action) const
{
	if (def.navOverride[action].mask != 0)
	{
		return def.navOverride[action].label;
	}
	switch (action)
	{
		case 0:
			return m_settings.keyUpLabel;
		case 1:
			return m_settings.keyDownLabel;
		case 2:
			return m_settings.keySelectLabel;
		default:
			return m_settings.keyBackLabel;
	}
}

bool MenuManager::DisplayMenu(MenuHandle menu, int slot, float duration, float curtime)
{
	if (slot < 0 || slot > MAXPLAYERS)
	{
		return false;
	}
	MenuDef *def = Find(menu);
	if (!def)
	{
		return false;
	}

	m_curtime = curtime;

	// Replace any existing menu first (fires its end callback).
	if (m_players[slot].active)
	{
		EndDisplay(slot, MenuEndReason::Cancelled);
	}

	PlayerMenu &pm = m_players[slot];
	pm.active = true;
	pm.handle = menu;
	pm.page = 0;
	pm.cursor = 0;
	pm.expireTime = (duration > 0.0f) ? (curtime + duration) : 0.0f;
	pm.prevButtons = 0;
	pm.buttonsPrimed = false;
	pm.nextHtmlRender = 0.0f;

	if (def->type == MenuType::Chat)
	{
		RenderPage(slot);
	}
	else
	{
		RenderHtml(slot);
	}
	return true;
}

void MenuManager::CancelMenu(int slot)
{
	if (slot < 0 || slot > MAXPLAYERS)
	{
		return;
	}
	EndDisplay(slot, MenuEndReason::Cancelled);
}

bool MenuManager::HasMenu(int slot) const
{
	if (slot < 0 || slot > MAXPLAYERS)
	{
		return false;
	}
	return m_players[slot].active;
}

MenuHandle MenuManager::GetActiveMenu(int slot) const
{
	if (slot < 0 || slot > MAXPLAYERS || !m_players[slot].active)
	{
		return kInvalidMenuHandle;
	}
	return m_players[slot].handle;
}

void MenuManager::DestroyMenu(MenuHandle menu)
{
	auto it = m_menus.find(menu);
	if (it == m_menus.end())
	{
		return;
	}

	MenuEndFn onEnd = it->second.onEnd;
	bool wasHtml = (it->second.type != MenuType::Chat);
	m_menus.erase(it);

	for (int slot = 0; slot <= MAXPLAYERS; slot++)
	{
		PlayerMenu &pm = m_players[slot];
		if (pm.active && pm.handle == menu)
		{
			pm.active = false;
			pm.handle = kInvalidMenuHandle;
			if (wasHtml)
			{
				center_html::Send(slot, "", 0); // clear the panel immediately
			}
			DepthGuard guard(m_callbackDepth);
			if (onEnd && guard.enter())
			{
				onEnd(menu, slot, MenuEndReason::Destroyed);
			}
		}
	}
}

int MenuManager::GetItemCount(MenuHandle menu) const
{
	const MenuDef *def = Find(menu);
	return def ? static_cast<int>(def->items.size()) : 0;
}

const char *MenuManager::GetItemText(MenuHandle menu, int item) const
{
	const MenuDef *def = Find(menu);
	if (!def || item < 0 || item >= static_cast<int>(def->items.size()))
	{
		return "";
	}
	return def->items[item].text.c_str();
}

const char *MenuManager::GetItemInfo(MenuHandle menu, int item) const
{
	const MenuDef *def = Find(menu);
	if (!def || item < 0 || item >= static_cast<int>(def->items.size()))
	{
		return "";
	}
	return def->items[item].info.c_str();
}

void MenuManager::EndDisplay(int slot, MenuEndReason reason)
{
	PlayerMenu &pm = m_players[slot];
	if (!pm.active)
	{
		return;
	}

	MenuHandle handle = pm.handle;
	pm.active = false;
	pm.handle = kInvalidMenuHandle;

	// Clear any HTML panel so it doesn't linger for its remaining duration.
	if (const MenuDef *def = Find(handle))
	{
		if (def->type != MenuType::Chat)
		{
			center_html::Send(slot, "", 0);
		}
	}

	// Copy the callback before invoking: the handler may DisplayMenu/DestroyMenu.
	if (MenuDef *def = Find(handle))
	{
		MenuEndFn onEnd = def->onEnd;
		DepthGuard guard(m_callbackDepth);
		if (onEnd && guard.enter())
		{
			onEnd(handle, slot, reason);
		}
	}
}

void MenuManager::Select(int slot, int itemIndex)
{
	PlayerMenu &pm = m_players[slot];
	if (!pm.active)
	{
		return;
	}
	MenuDef *def = Find(pm.handle);
	if (!def || itemIndex < 0 || itemIndex >= static_cast<int>(def->items.size()))
	{
		return;
	}

	if (def->items[itemIndex].disabled)
	{
		// Re-render so the player can pick again.
		if (def->type == MenuType::Chat)
		{
			RenderPage(slot);
		}
		else
		{
			RenderHtml(slot);
		}
		return;
	}

	MenuHandle handle = pm.handle;
	MenuItemSelectFn onSelect = def->onSelect;
	bool closeOnSelect = def->closeOnSelect;
	bool wasHtml = (def->type != MenuType::Chat);

	if (closeOnSelect)
	{
		pm.active = false;
		pm.handle = kInvalidMenuHandle;
		if (wasHtml)
		{
			center_html::Send(slot, "", 0);
		}

		{
			DepthGuard guard(m_callbackDepth);
			if (onSelect && guard.enter())
			{
				onSelect(handle, slot, itemIndex);
			}
		}

		if (MenuDef *ended = Find(handle))
		{
			MenuEndFn onEnd = ended->onEnd;
			DepthGuard guard(m_callbackDepth);
			if (onEnd && guard.enter())
			{
				onEnd(handle, slot, MenuEndReason::Selected);
			}
		}
	}
	else
	{
		{
			DepthGuard guard(m_callbackDepth);
			if (onSelect && guard.enter())
			{
				onSelect(handle, slot, itemIndex);
			}
		}
		// Re-render only if the handler left this same menu open.
		if (pm.active && pm.handle == handle)
		{
			if (const MenuDef *d = Find(handle); d && d->type == MenuType::Chat)
			{
				RenderPage(slot);
			}
			else
			{
				RenderHtml(slot);
			}
		}
	}
}

bool MenuManager::ProcessInput(int slot, const char *text, float curtime)
{
	if (slot < 0 || slot > MAXPLAYERS)
	{
		return false;
	}

	PlayerMenu &pm = m_players[slot];
	if (!pm.active)
	{
		return false;
	}

	MenuDef *def = Find(pm.handle);
	if (!def)
	{
		pm.active = false;
		pm.handle = kInvalidMenuHandle;
		return false;
	}

	// Chat input only drives chat menus, HTML menus use button polling.
	if (def->type != MenuType::Chat)
	{
		return false;
	}

	m_curtime = curtime;

	std::string msg(text ? text : "");
	if (msg.size() >= 2 && msg.front() == '"' && msg.back() == '"')
	{
		msg = msg.substr(1, msg.size() - 2);
	}

	int num;
	if (!IsNumericInput(msg.c_str(), m_settings.acceptedPrefixes, num))
	{
		return false;
	}

	const auto &items = def->items;
	int pageCount = (static_cast<int>(items.size()) + m_itemsPerPage - 1) / m_itemsPerPage;
	bool hasMore = (pm.page + 1 < pageCount);
	bool hasPrev = (pm.page > 0);

	int pageStart = pm.page * m_itemsPerPage;
	int pageEnd = (std::min)(pageStart + m_itemsPerPage, static_cast<int>(items.size()));
	int pageItems = pageEnd - pageStart;

	// Key layout: items 1..pageItems, Next = itemsPerPage+1, Prev = +2, Exit = 0.
	if (num == 0 && def->exitButton)
	{
		EndDisplay(slot, MenuEndReason::Exit);
		return true;
	}

	if (num == m_itemsPerPage + 1 && hasMore)
	{
		pm.page++;
		RenderPage(slot);
		return true;
	}

	if (num == m_itemsPerPage + 2 && hasPrev)
	{
		pm.page--;
		RenderPage(slot);
		return true;
	}

	if (num >= 1 && num <= pageItems)
	{
		Select(slot, pageStart + (num - 1));
		return true;
	}

	// Numeric but out of range: consume so it doesn't leak into chat.
	return true;
}

bool MenuManager::WantsButtonInput(int slot) const
{
	if (slot < 0 || slot > MAXPLAYERS || !m_players[slot].active)
	{
		return false;
	}
	const MenuDef *def = Find(m_players[slot].handle);
	return def && def->type != MenuType::Chat;
}

bool MenuManager::AnyHtmlMenuActive() const
{
	for (int i = 0; i <= MAXPLAYERS; i++)
	{
		if (!m_players[i].active)
		{
			continue;
		}
		const MenuDef *def = Find(m_players[i].handle);
		if (def && def->type != MenuType::Chat)
		{
			return true;
		}
	}
	return false;
}

void MenuManager::PollButtons(int slot, uint64_t heldButtons, float curtime)
{
	if (slot < 0 || slot > MAXPLAYERS)
	{
		return;
	}
	PlayerMenu &pm = m_players[slot];
	if (!pm.active)
	{
		return;
	}
	MenuDef *def = Find(pm.handle);
	if (!def || def->type == MenuType::Chat)
	{
		return;
	}

	m_curtime = curtime;

	// First poll establishes a baseline so a held key doesn't fire immediately.
	if (!pm.buttonsPrimed)
	{
		pm.prevButtons = heldButtons;
		pm.buttonsPrimed = true;
		return;
	}

	uint64_t newly = heldButtons & ~pm.prevButtons;
	pm.prevButtons = heldButtons;
	if (newly == 0)
	{
		return;
	}

	if (newly & EffectiveNavMask(*def, 0))
	{
		HtmlMoveCursor(slot, -1);
	}
	else if (newly & EffectiveNavMask(*def, 1))
	{
		HtmlMoveCursor(slot, +1);
	}
	else if (newly & EffectiveNavMask(*def, 2))
	{
		// Selecting the inline Exit row exits WOW.
		if (HtmlShowsExitRow(*def) && pm.cursor == static_cast<int>(def->items.size()))
		{
			EndDisplay(slot, MenuEndReason::Exit);
		}
		else
		{
			Select(slot, pm.cursor);
		}
	}
	else if (newly & EffectiveNavMask(*def, 3))
	{
		if (def->exitButton)
		{
			EndDisplay(slot, MenuEndReason::Exit);
		}
	}
}

void MenuManager::HtmlMoveCursor(int slot, int delta)
{
	PlayerMenu &pm = m_players[slot];
	const MenuDef *def = Find(pm.handle);
	if (!def)
	{
		return;
	}

	int count = HtmlRowCount(*def); // includes the inline Exit row, if any
	if (count <= 0)
	{
		return;
	}
	// Wrap around: past the bottom returns to the top and vice versa.
	// This also lets a single bound key cycle through every row.
	int next = ((pm.cursor + delta) % count + count) % count;
	if (next == pm.cursor)
	{
		return;
	}
	pm.cursor = next;
	RenderHtml(slot);
}

void MenuManager::Tick(float curtime)
{
	m_curtime = curtime;
	for (int i = 0; i <= MAXPLAYERS; i++)
	{
		PlayerMenu &pm = m_players[i];
		if (!pm.active)
		{
			continue;
		}
		if (pm.expireTime > 0.0f && curtime >= pm.expireTime)
		{
			EndDisplay(i, MenuEndReason::Timeout);
			continue;
		}
		// HTML messages decay, refresh periodically.
		const MenuDef *def = Find(pm.handle);
		if (def && def->type != MenuType::Chat && curtime >= pm.nextHtmlRender)
		{
			RenderHtml(i);
		}
	}
}

void MenuManager::OnPlayerDisconnect(int slot)
{
	if (slot < 0 || slot > MAXPLAYERS)
	{
		return;
	}
	EndDisplay(slot, MenuEndReason::Disconnect);
}

void MenuManager::Shutdown()
{
	for (int i = 0; i <= MAXPLAYERS; i++)
	{
		m_players[i].active = false;
		m_players[i].handle = kInvalidMenuHandle;
	}
	m_menus.clear();
}

void MenuManager::Configure(const MenuManagerSettings &settings)
{
	m_settings = settings;

	m_itemsPerPage = (std::max)(1, (std::min)(settings.itemsPerPage, MENU_MAX_ITEMS_PER_PAGE));
	m_htmlVisibleItems = (std::max)(1, (std::min)(settings.htmlVisibleItems, MENU_MAX_HTML_VISIBLE));

	// MenuType::Default would be circular, treat it (and unknown) as Chat.
	if (m_settings.defaultType == MenuType::Default)
	{
		m_settings.defaultType = MenuType::Chat;
	}
}

void MenuManager::SetHtmlAvailable(bool available)
{
	m_htmlAvailable = available;
}

void MenuManager::RenderPage(int slot)
{
	PlayerMenu &pm = m_players[slot];
	if (!pm.active)
	{
		return;
	}

	MenuDef *def = Find(pm.handle);
	if (!def)
	{
		pm.active = false;
		pm.handle = kInvalidMenuHandle;
		return;
	}

	const auto &items = def->items;

	int pageCount = (static_cast<int>(items.size()) + m_itemsPerPage - 1) / m_itemsPerPage;
	if (pageCount == 0)
	{
		pageCount = 1;
	}

	bool hasMore = (pm.page + 1 < pageCount);
	bool hasPrev = (pm.page > 0);

	int pageStart = pm.page * m_itemsPerPage;
	int pageEnd = (std::min)(pageStart + m_itemsPerPage, static_cast<int>(items.size()));
	int pageItems = pageEnd - pageStart;

	char buf[256];
	if (pageCount > 1)
	{
		snprintf(buf, sizeof(buf), "\x04-- %s \x01(page %d/%d) --", def->title.c_str(), pm.page + 1, pageCount);
	}
	else
	{
		snprintf(buf, sizeof(buf), "\x04-- %s --", def->title.c_str());
	}
	MENU_PrintToChat(slot, "%s", buf);

	for (int i = 0; i < pageItems; i++)
	{
		const MenuItem &item = items[pageStart + i];
		if (item.disabled)
		{
			snprintf(buf, sizeof(buf), "\x08#%d %s", i + 1, item.text.c_str());
		}
		else
		{
			snprintf(buf, sizeof(buf), "\x01#%d %s", i + 1, item.text.c_str());
		}
		MENU_PrintToChat(slot, "%s", buf);
	}

	if (hasMore)
	{
		snprintf(buf, sizeof(buf), "\x01#%d \x04-> Next Page", m_itemsPerPage + 1);
		MENU_PrintToChat(slot, "%s", buf);
	}
	if (hasPrev)
	{
		snprintf(buf, sizeof(buf), "\x01#%d \x04-> Previous Page", m_itemsPerPage + 2);
		MENU_PrintToChat(slot, "%s", buf);
	}
	if (def->exitButton)
	{
		MENU_PrintToChat(slot, "\x01#0 \x04-> Exit");
	}
}

void MenuManager::RenderHtml(int slot)
{
	PlayerMenu &pm = m_players[slot];
	if (!pm.active)
	{
		return;
	}

	MenuDef *def = Find(pm.handle);
	if (!def)
	{
		pm.active = false;
		pm.handle = kInvalidMenuHandle;
		return;
	}

	pm.nextHtmlRender = m_curtime + kHtmlRefreshInterval;

	const auto &items = def->items;
	int itemCount = static_cast<int>(items.size());
	bool exitRow = HtmlShowsExitRow(*def);
	int count = itemCount + (exitRow ? 1 : 0); // navigable rows (Exit is the last)

	if (pm.cursor < 0)
	{
		pm.cursor = 0;
	}
	else if (count > 0 && pm.cursor >= count)
	{
		pm.cursor = count - 1;
	}

	std::string html;
	html.reserve(512);

	// Title + position counter.
	html += "<font color='#FFFFFF' class='fontSize-m'>";
	html += center_html::Escape(def->title);
	html += "</font>";
	if (count > 0)
	{
		char counter[64];
		snprintf(counter, sizeof(counter), " <font class='fontSize-s' color='#FFFFFF'>[%d/%d]</font>", pm.cursor + 1, count);
		html += counter;
	}
	html += "<br>";

	// Scrolling window centered on the cursor.
	int vis = (std::min)(m_htmlVisibleItems, count);
	int start = pm.cursor - vis / 2;
	if (start < 0)
	{
		start = 0;
	}
	if (start + vis > count)
	{
		start = (std::max)(0, count - vis);
	}

	for (int i = start; i < start + vis; i++)
	{
		bool selected = (i == pm.cursor);

		// The inline Exit row sits at index == itemCount (after the real items).
		if (i >= itemCount)
		{
			if (selected)
			{
				html += "<font color='";
				html += m_settings.navColor;
				html += "' class='fontSize-sm'>";
				html += kHtmlMarker;
				html += "</font>";
			}
			html += "<font color='";
			html += selected ? m_settings.navColor : m_settings.footerColor;
			html += "' class='fontSize-sm'>Exit</font><br>";
			continue;
		}

		const MenuItem &item = items[i];

		if (selected)
		{
			html += "<font color='";
			html += m_settings.navColor;
			html += "' class='fontSize-sm'>";
			html += kHtmlMarker;
			html += "</font>";
		}

		const char *color = item.disabled ? m_settings.disabledColor.c_str() : (selected ? m_settings.navColor.c_str() : "#FFFFFF");
		html += "<font color='";
		html += color;
		html += "' class='fontSize-sm'>";
		html += center_html::Escape(item.text);
		html += "</font><br>";
	}

	// Footer with key hints. Reflects per-menu overrides / configured nav keys,
	// and adapts when a direction is disabled (single-key scroll shows "Scroll: KEY").
	bool upOn = EffectiveNavMask(*def, 0) != 0;
	bool downOn = EffectiveNavMask(*def, 1) != 0;
	bool selectOn = EffectiveNavMask(*def, 2) != 0;
	bool backOn = def->exitButton && EffectiveNavMask(*def, 3) != 0;

	std::string footer;
	auto addSegment = [&footer](const std::string &seg)
	{
		if (seg.empty())
		{
			return;
		}
		if (!footer.empty())
		{
			footer += " | ";
		}
		footer += seg;
	};

	if (upOn && downOn)
	{
		addSegment("Move: " + EffectiveNavLabel(*def, 0) + "/" + EffectiveNavLabel(*def, 1));
	}
	else if (downOn)
	{
		addSegment("Scroll: " + EffectiveNavLabel(*def, 1));
	}
	else if (upOn)
	{
		addSegment("Scroll: " + EffectiveNavLabel(*def, 0));
	}
	if (selectOn)
	{
		addSegment("Select: " + EffectiveNavLabel(*def, 2));
	}
	if (backOn)
	{
		addSegment("Exit: " + EffectiveNavLabel(*def, 3));
	}

	html += "<font color='";
	html += m_settings.footerColor;
	html += "' class='fontSize-s'>";
	html += footer;
	html += "</font>";

	center_html::Send(slot, html.c_str(), kHtmlDurationSecs);
}
