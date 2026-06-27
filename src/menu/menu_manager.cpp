#include "menu_manager.h"
#include "src/entity/in_buttons.h"
#include "src/lang/translations.h"
#include "src/render/center_html.h"
#include "src/utils/print_utils.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>

// Recursive so a callback can re-enter the API on the same thread.
using ScopedLock = std::lock_guard<std::recursive_mutex>;

// Per-menu override value meaning "explicitly disabled" (MenuButton::None),
// as opposed to mask 0 which means "inherit the server config binding".
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

// Map an HTML size token to its Panorama fontSize class (px in csgostyles.css):
// xs 8, s 12, sm 16, m 18, ml 20, l 24, xl 32, xxl 40, xxxl 64.
// An already-qualified "fontSize-..." string passes through. Unknown -> "".
static std::string SizeClass(const std::string &tok)
{
	if (tok.rfind("fontSize-", 0) == 0)
	{
		return tok;
	}
	if (tok == "xs" || tok == "s" || tok == "sm" || tok == "m" || tok == "ml" || tok == "l" || tok == "xl" || tok == "xxl" || tok == "xxxl")
	{
		return "fontSize-" + tok;
	}
	return "";
}

// Map a line-alignment token to its Panorama block-align class suffix.
// left -> "" (the default), center -> horizontal-center, right -> horizontal-align-right.
static std::string AlignClass(const std::string &align)
{
	if (align == "right")
	{
		return " horizontal-align-right";
	}
	if (align == "left")
	{
		return "";
	}
	return " horizontal-center"; // center (and any unexpected value)
}

// Valid line-alignment tokens. Returns "" for anything else so the caller can ignore it.
static std::string NormalizeAlign(const std::string &v)
{
	if (v == "left" || v == "center" || v == "right")
	{
		return v;
	}
	return "";
}

MenuManager g_MenuManager;

// Content + duration used to clear a closed menu's panel.
// An empty loc_token never decays, so we send a non-empty but invisible payload with a short TTL:
// it renders to nothing and then expires.
static constexpr int kHtmlClearDurationSecs = 1;
static const char *kHtmlClearContent = "<font></font>";

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

void MenuManager::SetMainThread()
{
	ScopedLock lock(m_mutex);
	m_mainThread = std::this_thread::get_id();
}

bool MenuManager::OnMainThread() const
{
	// Until SetMainThread runs (Load), the default id counts as main: early calls run inline.
	return m_mainThread == std::thread::id {} || std::this_thread::get_id() == m_mainThread;
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
	ScopedLock lock(m_mutex);
	MenuHandle h = m_nextHandle++;
	MenuDef def;
	// Store the base type as-is (including the Default sentinel).
	// The effective type is resolved per viewer at display time (see ResolveType),
	// where the player's preference and HTML availability are applied.
	def.type = type;
	def.title = title ? title : "";
	def.onSelect = std::move(onSelect);
	def.exitButton = m_settings.defaultExitButton;
	def.exitItem = m_settings.defaultExitItem;
	for (int i = 0; i < static_cast<int>(MenuLabel::Count); i++)
	{
		def.labels[i] = DefaultLabelKey(static_cast<MenuLabel>(i));
	}
	m_menus.emplace(h, std::move(def));
	return h;
}

int MenuManager::AddItem(MenuHandle menu, const char *text, const char *info, bool disabled)
{
	ScopedLock lock(m_mutex);
	MenuDef *def = Find(menu);
	if (!def)
	{
		return -1;
	}
	def->items.push_back({text ? text : "", info ? info : "", disabled});
	return static_cast<int>(def->items.size()) - 1;
}

int MenuManager::AddSubMenu(MenuHandle parent, const char *text, MenuHandle child, const char *info)
{
	ScopedLock lock(m_mutex);
	MenuDef *parentDef = Find(parent);
	MenuDef *childDef = Find(child);
	if (!parentDef || !childDef || child == parent)
	{
		return -1;
	}

	childDef->parent = parent; // so Back in the child returns here
	MenuItem item;
	item.text = text ? text : "";
	item.info = info ? info : "";
	item.submenu = child;
	parentDef->items.push_back(std::move(item));
	return static_cast<int>(parentDef->items.size()) - 1;
}

void MenuManager::SetTitle(MenuHandle menu, const char *title)
{
	ScopedLock lock(m_mutex);
	if (MenuDef *def = Find(menu))
	{
		def->title = title ? title : "";
	}
}

void MenuManager::SetExitButton(MenuHandle menu, bool enabled)
{
	ScopedLock lock(m_mutex);
	if (MenuDef *def = Find(menu))
	{
		def->exitButton = enabled;
	}
}

void MenuManager::SetCloseOnSelect(MenuHandle menu, bool enabled)
{
	ScopedLock lock(m_mutex);
	if (MenuDef *def = Find(menu))
	{
		def->closeOnSelect = enabled;
	}
}

void MenuManager::SetMenuEndCallback(MenuHandle menu, MenuEndFn onEnd)
{
	ScopedLock lock(m_mutex);
	if (MenuDef *def = Find(menu))
	{
		def->onEnd = std::move(onEnd);
	}
}

void MenuManager::SetMenuKey(MenuHandle menu, MenuNavAction action, MenuButton button)
{
	ScopedLock lock(m_mutex);
	MenuDef *def = Find(menu);
	int idx = static_cast<int>(action);
	if (!def || idx < 0 || idx > static_cast<int>(MenuNavAction::Back))
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
	ScopedLock lock(m_mutex);
	if (MenuDef *def = Find(menu))
	{
		def->exitItem = enabled;
	}
}

void MenuManager::SetMenuForceType(MenuHandle menu, bool force)
{
	ScopedLock lock(m_mutex);
	if (MenuDef *def = Find(menu))
	{
		def->forced = force;
	}
}

void MenuManager::SetMenuLabel(MenuHandle menu, MenuLabel label, const char *text)
{
	ScopedLock lock(m_mutex);
	MenuDef *def = Find(menu);
	int idx = static_cast<int>(label);
	if (!def || idx < 0 || idx >= static_cast<int>(MenuLabel::Count))
	{
		return;
	}
	// Empty restores the built-in key.
	// A non-empty value is a phrase key (translated per viewer) or literal text when no phrase matches.
	def->labels[idx] = (text && text[0]) ? text : DefaultLabelKey(label);
	RefreshMenu(menu);
}

// Returned pointer aliases internal storage: valid only until the next mutating call, don't cache it.
const char *MenuManager::GetMenuLabel(MenuHandle menu, MenuLabel label) const
{
	ScopedLock lock(m_mutex);
	const MenuDef *def = Find(menu);
	int idx = static_cast<int>(label);
	if (!def || idx < 0 || idx >= static_cast<int>(MenuLabel::Count))
	{
		return "";
	}
	return def->labels[idx].c_str();
}

void MenuManager::SetMenuStyle(MenuHandle menu, MenuStyle field, const char *value)
{
	ScopedLock lock(m_mutex);
	MenuDef *def = Find(menu);
	if (!def)
	{
		return;
	}
	std::string v = value ? value : "";
	StyleOverride &s = def->style;
	switch (field)
	{
		case MenuStyle::TitleColor:
			s.titleColor = v;
			break;
		case MenuStyle::NavColor:
			s.navColor = v;
			break;
		case MenuStyle::FooterColor:
			s.footerColor = v;
			break;
		case MenuStyle::DisabledColor:
			s.disabledColor = v;
			break;
		case MenuStyle::ItemColor:
			s.itemColor = v;
			break;
		case MenuStyle::FontFace:
			s.fontFace = v;
			break;
		case MenuStyle::Marker:
			s.marker = v;
			break;
		case MenuStyle::CounterColor:
			s.counterColor = v;
			break;
		case MenuStyle::SubmenuSuffix:
			s.submenuSuffix = v;
			break;
		case MenuStyle::FooterSeparator:
			s.footerSeparator = v;
			break;
		case MenuStyle::CounterPrefix:
			s.counterPrefix = v;
			break;
		case MenuStyle::CounterSuffix:
			s.counterSuffix = v;
			break;
		case MenuStyle::TitleSize:
			// Empty clears the override, an unknown token is ignored so a typo can't blank the size.
			if (v.empty() || !SizeClass(v).empty())
			{
				s.titleSize = v;
			}
			break;
		case MenuStyle::ItemSize:
			if (v.empty() || !SizeClass(v).empty())
			{
				s.itemSize = v;
			}
			break;
		case MenuStyle::FooterSize:
			if (v.empty() || !SizeClass(v).empty())
			{
				s.footerSize = v;
			}
			break;
		case MenuStyle::Align:
			// Empty clears the override, an unrecognized token is ignored.
			if (v.empty())
			{
				s.align.clear();
			}
			else if (std::string a = NormalizeAlign(v); !a.empty())
			{
				s.align = a;
			}
			break;
		case MenuStyle::ShowCounter:
			s.showCounter = v.empty() ? -1 : (v != "0" ? 1 : 0);
			break;
		case MenuStyle::ShowFooter:
			s.showFooter = v.empty() ? -1 : (v != "0" ? 1 : 0);
			break;
		case MenuStyle::HighlightText:
			s.highlightText = v.empty() ? -1 : (v != "0" ? 1 : 0);
			break;
	}
	RefreshMenu(menu);
}

// Returned pointer aliases internal storage: valid only until the next mutating call, don't cache it.
const char *MenuManager::GetMenuStyle(MenuHandle menu, MenuStyle field) const
{
	ScopedLock lock(m_mutex);
	const MenuDef *def = Find(menu);
	if (!def)
	{
		return "";
	}
	const StyleOverride &s = def->style;
	switch (field)
	{
		case MenuStyle::TitleColor:
			return s.titleColor.empty() ? m_settings.titleColor.c_str() : s.titleColor.c_str();
		case MenuStyle::NavColor:
			return s.navColor.empty() ? m_settings.navColor.c_str() : s.navColor.c_str();
		case MenuStyle::FooterColor:
			return s.footerColor.empty() ? m_settings.footerColor.c_str() : s.footerColor.c_str();
		case MenuStyle::DisabledColor:
			return s.disabledColor.empty() ? m_settings.disabledColor.c_str() : s.disabledColor.c_str();
		case MenuStyle::ItemColor:
			return s.itemColor.empty() ? m_settings.itemColor.c_str() : s.itemColor.c_str();
		case MenuStyle::FontFace:
			return s.fontFace.empty() ? m_settings.fontFace.c_str() : s.fontFace.c_str();
		case MenuStyle::Marker:
			return s.marker.empty() ? m_settings.marker.c_str() : s.marker.c_str();
		case MenuStyle::CounterColor:
			return s.counterColor.empty() ? m_settings.counterColor.c_str() : s.counterColor.c_str();
		case MenuStyle::SubmenuSuffix:
			return s.submenuSuffix.empty() ? m_settings.submenuSuffix.c_str() : s.submenuSuffix.c_str();
		case MenuStyle::FooterSeparator:
			return s.footerSeparator.empty() ? m_settings.footerSeparator.c_str() : s.footerSeparator.c_str();
		case MenuStyle::CounterPrefix:
			return s.counterPrefix.empty() ? m_settings.counterPrefix.c_str() : s.counterPrefix.c_str();
		case MenuStyle::CounterSuffix:
			return s.counterSuffix.empty() ? m_settings.counterSuffix.c_str() : s.counterSuffix.c_str();
		case MenuStyle::TitleSize:
			return s.titleSize.empty() ? m_settings.titleSize.c_str() : s.titleSize.c_str();
		case MenuStyle::ItemSize:
			return s.itemSize.empty() ? m_settings.itemSize.c_str() : s.itemSize.c_str();
		case MenuStyle::FooterSize:
			return s.footerSize.empty() ? m_settings.footerSize.c_str() : s.footerSize.c_str();
		case MenuStyle::Align:
			return s.align.empty() ? m_settings.align.c_str() : s.align.c_str();
		case MenuStyle::ShowCounter:
			return (s.showCounter < 0 ? m_settings.showCounter : s.showCounter != 0) ? "1" : "0";
		case MenuStyle::ShowFooter:
			return (s.showFooter < 0 ? m_settings.showFooter : s.showFooter != 0) ? "1" : "0";
		case MenuStyle::HighlightText:
			return (s.highlightText < 0 ? m_settings.highlightText : s.highlightText != 0) ? "1" : "0";
	}
	return "";
}

void MenuManager::SetItemText(MenuHandle menu, int item, const char *text)
{
	ScopedLock lock(m_mutex);
	MenuDef *def = Find(menu);
	if (!def || item < 0 || item >= static_cast<int>(def->items.size()))
	{
		return;
	}
	def->items[item].text = text ? text : "";
	RefreshMenu(menu);
}

void MenuManager::SetItemInfo(MenuHandle menu, int item, const char *info)
{
	ScopedLock lock(m_mutex);
	MenuDef *def = Find(menu);
	if (!def || item < 0 || item >= static_cast<int>(def->items.size()))
	{
		return;
	}
	def->items[item].info = info ? info : ""; // info isn't rendered, no refresh
}

bool MenuManager::GetItemDisabled(MenuHandle menu, int item) const
{
	ScopedLock lock(m_mutex);
	const MenuDef *def = Find(menu);
	if (!def || item < 0 || item >= static_cast<int>(def->items.size()))
	{
		return false;
	}
	return def->items[item].disabled;
}

void MenuManager::SetItemDisabled(MenuHandle menu, int item, bool disabled)
{
	ScopedLock lock(m_mutex);
	MenuDef *def = Find(menu);
	if (!def || item < 0 || item >= static_cast<int>(def->items.size()))
	{
		return;
	}
	def->items[item].disabled = disabled;
	RefreshMenu(menu);
}

bool MenuManager::GetItemRaw(MenuHandle menu, int item) const
{
	ScopedLock lock(m_mutex);
	const MenuDef *def = Find(menu);
	if (!def || item < 0 || item >= static_cast<int>(def->items.size()))
	{
		return false;
	}
	return def->items[item].raw;
}

void MenuManager::SetItemRaw(MenuHandle menu, int item, bool raw)
{
	ScopedLock lock(m_mutex);
	MenuDef *def = Find(menu);
	if (!def || item < 0 || item >= static_cast<int>(def->items.size()))
	{
		return;
	}
	def->items[item].raw = raw;
	RefreshMenu(menu);
}

// Returned pointer aliases internal storage: valid only until the next mutating call, don't cache it.
const char *MenuManager::GetItemIcon(MenuHandle menu, int item) const
{
	ScopedLock lock(m_mutex);
	const MenuDef *def = Find(menu);
	if (!def || item < 0 || item >= static_cast<int>(def->items.size()))
	{
		return "";
	}
	return def->items[item].iconUrl.c_str();
}

void MenuManager::SetItemIcon(MenuHandle menu, int item, const char *url)
{
	ScopedLock lock(m_mutex);
	MenuDef *def = Find(menu);
	if (!def || item < 0 || item >= static_cast<int>(def->items.size()))
	{
		return;
	}
	def->items[item].iconUrl = url ? url : "";
	RefreshMenu(menu);
}

void MenuManager::RemoveItem(MenuHandle menu, int item)
{
	ScopedLock lock(m_mutex);
	MenuDef *def = Find(menu);
	if (!def || item < 0 || item >= static_cast<int>(def->items.size()))
	{
		return;
	}
	def->items.erase(def->items.begin() + item);
	RefreshMenu(menu); // RenderHtml/RenderPage clamp any now-stale cursor/page
}

void MenuManager::RemoveAllItems(MenuHandle menu)
{
	ScopedLock lock(m_mutex);
	MenuDef *def = Find(menu);
	if (!def)
	{
		return;
	}
	def->items.clear();
	// Reset display state for anyone viewing it, then re-render.
	for (int slot = 0; slot <= MAXPLAYERS; slot++)
	{
		PlayerMenu &pm = m_players[slot];
		if (pm.active && pm.handle == menu)
		{
			pm.cursor = 0;
			pm.page = 0;
		}
	}
	RefreshMenu(menu);
}

void MenuManager::SetStartItem(MenuHandle menu, int item)
{
	ScopedLock lock(m_mutex);
	if (MenuDef *def = Find(menu))
	{
		def->startItem = (item > 0) ? item : 0;
	}
}

int MenuManager::GetStartItem(MenuHandle menu) const
{
	ScopedLock lock(m_mutex);
	const MenuDef *def = Find(menu);
	return def ? def->startItem : 0;
}

bool MenuManager::HtmlShowsExitRow(const MenuDef &def, int slot) const
{
	// Must be exitable, and either explicitly requested or forced because the Back
	// key is disabled (otherwise the player would have no way out but the timeout).
	return def.exitButton && (def.exitItem || EffectiveNavMask(def, slot, MenuNavAction::Back) == 0);
}

int MenuManager::HtmlRowCount(const MenuDef &def, int slot) const
{
	return static_cast<int>(def.items.size()) + (HtmlShowsExitRow(def, slot) ? 1 : 0);
}

uint64_t MenuManager::EffectiveNavMask(const MenuDef &def, int slot, MenuNavAction action) const
{
	// 1. Per-menu override set by the consumer (wins, including an explicit disable).
	uint64_t override_ = def.navOverride[static_cast<int>(action)].mask;
	if (override_ == kNavDisabledSentinel)
	{
		return 0; // explicitly disabled for this menu (MenuButton::None)
	}
	if (override_ != 0)
	{
		return override_;
	}
	// 2. This player's preference (displaces only the server default binding).
	if (slot >= 0 && slot <= MAXPLAYERS)
	{
		uint64_t pref = m_prefs[slot].nav[static_cast<int>(action)].mask;
		if (pref == kNavDisabledSentinel)
		{
			return 0;
		}
		if (pref != 0)
		{
			return pref;
		}
	}
	// 3. Server config.
	switch (action)
	{
		case MenuNavAction::Up:
			return m_settings.keyUp;
		case MenuNavAction::Down:
			return m_settings.keyDown;
		case MenuNavAction::Select:
			return m_settings.keySelect;
		default:
			return m_settings.keyBack;
	}
}

std::string MenuManager::EffectiveNavLabel(const MenuDef &def, int slot, MenuNavAction action) const
{
	if (def.navOverride[static_cast<int>(action)].mask != 0)
	{
		return def.navOverride[static_cast<int>(action)].label;
	}
	if (slot >= 0 && slot <= MAXPLAYERS)
	{
		const NavOverride &pref = m_prefs[slot].nav[static_cast<int>(action)];
		if (pref.mask != 0)
		{
			return pref.label;
		}
	}
	switch (action)
	{
		case MenuNavAction::Up:
			return m_settings.keyUpLabel;
		case MenuNavAction::Down:
			return m_settings.keyDownLabel;
		case MenuNavAction::Select:
			return m_settings.keySelectLabel;
		default:
			return m_settings.keyBackLabel;
	}
}

MenuType MenuManager::ResolveType(const MenuDef &def, int slot) const
{
	MenuType base = (def.type == MenuType::Default) ? m_settings.defaultType : def.type;
	MenuType result = base;
	if (!def.forced && slot >= 0 && slot <= MAXPLAYERS && m_prefs[slot].type != MenuType::Default)
	{
		result = m_prefs[slot].type;
	}
	// HTML must be available to render + receive input, else fall back to chat.
	if (result != MenuType::Chat && !m_htmlAvailable)
	{
		result = MenuType::Chat;
	}
	return result;
}

const char *MenuManager::DefaultLabelKey(MenuLabel label)
{
	// These double as the phrase keys in cs2menus.phrases.txt.
	switch (label)
	{
		case MenuLabel::NextPage:
			return "Next Page";
		case MenuLabel::PrevPage:
			return "Previous Page";
		case MenuLabel::Move:
			return "Move";
		case MenuLabel::Scroll:
			return "Scroll";
		case MenuLabel::Select:
			return "Select";
		case MenuLabel::Exit:
		default:
			return "Exit";
	}
}

std::string MenuManager::ResolveLabel(int slot, const MenuDef &def, MenuLabel label) const
{
	const std::string &key = def.labels[static_cast<int>(label)];
	std::string lang = m_langResolver ? m_langResolver(slot) : std::string();
	return g_Translations.Translate(lang, key);
}

bool MenuManager::DisplayMenu(MenuHandle menu, int slot, float duration, float curtime)
{
	ScopedLock lock(m_mutex);
	if (slot < 0 || slot > MAXPLAYERS)
	{
		return false;
	}
	if (!Find(menu))
	{
		return false;
	}

	// Render + possible Cancelled callback are main-thread only.
	// Off-thread defers to GameFrame.
	// The bool then reports that the display was queued, not that it will succeed:
	// a slot held by externalBusy (or a menu destroyed before the drain) can still no-op when GameFrame runs it.
	if (!OnMainThread())
	{
		m_pending.push_back(
			[this, menu, slot, duration]
			{
				if (Find(menu))
				{
					DisplayLocked(menu, slot, duration);
				}
			});
		return true;
	}

	m_curtime = curtime;
	return DisplayLocked(menu, slot, duration);
}

bool MenuManager::DisplayLocked(MenuHandle menu, int slot, float duration)
{
	MenuDef *def = Find(menu);
	if (!def)
	{
		return false;
	}

	// A host UI owns this slot. Stay out of its way.
	if (m_players[slot].externalBusy)
	{
		return false;
	}

	// Replace any existing menu first (fires its end callback).
	if (m_players[slot].active)
	{
		EndDisplay(slot, MenuEndReason::Cancelled);
	}

	PlayerMenu &pm = m_players[slot];
	pm.active = true;
	pm.handle = menu;
	// Resolve the render type for this viewer (forced menu, else their preference, then HTML availability).
	// Fixed for the life of this display.
	pm.type = ResolveType(*def, slot);
	// Open on the configured start item (clamped at render time).
	// m_itemsPerPage is clamped >= 1 in Configure, so the divide is safe.
	pm.cursor = def->startItem;
	pm.page = def->startItem / m_itemsPerPage;
	pm.expireTime = (duration > 0.0f) ? (m_curtime + duration) : 0.0f;
	pm.prevButtons = 0;
	pm.buttonsPrimed = false;
	pm.nextHtmlRender = 0.0f;
	pm.lastHtml.clear();
	pm.lastHtmlSend = 0.0f;

	Render(slot);
	return true;
}

void MenuManager::CancelMenu(int slot)
{
	ScopedLock lock(m_mutex);
	if (slot < 0 || slot > MAXPLAYERS)
	{
		return;
	}
	// HTML clear + end-callback are main-thread only.
	// Off-thread defers to GameFrame.
	if (!OnMainThread())
	{
		m_pending.push_back([this, slot] { EndDisplay(slot, MenuEndReason::Cancelled); });
		return;
	}
	EndDisplay(slot, MenuEndReason::Cancelled);
}

bool MenuManager::HasMenu(int slot) const
{
	ScopedLock lock(m_mutex);
	if (slot < 0 || slot > MAXPLAYERS)
	{
		return false;
	}
	return m_players[slot].active;
}

MenuHandle MenuManager::GetActiveMenu(int slot) const
{
	ScopedLock lock(m_mutex);
	if (slot < 0 || slot > MAXPLAYERS || !m_players[slot].active)
	{
		return kInvalidMenuHandle;
	}
	return m_players[slot].handle;
}

MenuType MenuManager::GetActiveMenuType(int slot) const
{
	ScopedLock lock(m_mutex);
	if (slot < 0 || slot > MAXPLAYERS || !m_players[slot].active)
	{
		return MenuType::Chat;
	}
	// Report the resolved per-viewer type so callers see chat vs html as actually rendered.
	return m_players[slot].type;
}

int MenuManager::GetSelectedItem(int slot) const
{
	ScopedLock lock(m_mutex);
	if (slot < 0 || slot > MAXPLAYERS || !m_players[slot].active)
	{
		return -1;
	}
	const PlayerMenu &pm = m_players[slot];
	const MenuDef *def = Find(pm.handle);
	// HTML only, and only when the cursor sits on a real item (not the Exit row).
	if (!def || pm.type == MenuType::Chat || pm.cursor < 0 || pm.cursor >= static_cast<int>(def->items.size()))
	{
		return -1;
	}
	return pm.cursor;
}

void MenuManager::DestroyMenu(MenuHandle menu)
{
	ScopedLock lock(m_mutex);
	auto it = m_menus.find(menu);
	if (it == m_menus.end())
	{
		return;
	}

	MenuEndFn onEnd = it->second.onEnd;
	bool wasHtml = (it->second.type != MenuType::Chat);

	// Erase now: handle is invalid on return (API contract) and no render can resurrect it.
	m_menus.erase(it);

	bool onMain = OnMainThread();

	for (int slot = 0; slot <= MAXPLAYERS; slot++)
	{
		PlayerMenu &pm = m_players[slot];
		if (!(pm.active && pm.handle == menu))
		{
			continue;
		}
		pm.active = false;
		pm.handle = kInvalidMenuHandle;

		if (onMain)
		{
			if (wasHtml)
			{
				center_html::Send(slot, kHtmlClearContent, kHtmlClearDurationSecs); // clear the panel immediately
			}
			DepthGuard guard(m_callbackDepth);
			if (onEnd && guard.enter())
			{
				onEnd(menu, slot, MenuEndReason::Destroyed);
			}
		}
		else if (wasHtml)
		{
			// Defer the panel clear to main. The Destroyed callback is skipped:
			// the consumer initiated this, and running its lambda off-thread is unsafe.
			m_pending.push_back([this, slot] { center_html::Send(slot, kHtmlClearContent, kHtmlClearDurationSecs); });
		}
	}
}

int MenuManager::GetItemCount(MenuHandle menu) const
{
	ScopedLock lock(m_mutex);
	const MenuDef *def = Find(menu);
	return def ? static_cast<int>(def->items.size()) : 0;
}

// Returned pointer aliases internal storage: valid only until the next mutating call, don't cache it.
const char *MenuManager::GetItemText(MenuHandle menu, int item) const
{
	ScopedLock lock(m_mutex);
	const MenuDef *def = Find(menu);
	if (!def || item < 0 || item >= static_cast<int>(def->items.size()))
	{
		return "";
	}
	return def->items[item].text.c_str();
}

// Same aliasing caveat as GetItemText.
const char *MenuManager::GetItemInfo(MenuHandle menu, int item) const
{
	ScopedLock lock(m_mutex);
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
	if (Find(handle) && pm.type != MenuType::Chat)
	{
		center_html::Send(slot, kHtmlClearContent, kHtmlClearDurationSecs);
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
		Render(slot); // re-render so the player can pick again
		return;
	}

	// A submenu item navigates into its child instead of firing onSelect.
	MenuHandle sub = def->items[itemIndex].submenu;
	if (sub != kInvalidMenuHandle && Find(sub))
	{
		SwitchMenu(slot, sub);
		return;
	}

	MenuHandle handle = pm.handle;
	MenuItemSelectFn onSelect = def->onSelect;
	bool closeOnSelect = def->closeOnSelect;
	bool wasHtml = (pm.type != MenuType::Chat);

	if (closeOnSelect)
	{
		pm.active = false;
		pm.handle = kInvalidMenuHandle;
		if (wasHtml)
		{
			center_html::Send(slot, kHtmlClearContent, kHtmlClearDurationSecs);
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
			Render(slot);
		}
	}
}

void MenuManager::SwitchMenu(int slot, MenuHandle handle)
{
	PlayerMenu &pm = m_players[slot];
	const MenuDef *newDef = Find(handle);
	if (!newDef)
	{
		return;
	}

	// Re-resolve the render type for the menu we're switching to (a submenu may be forced to a different type than the parent).
	// Clear the HTML panel only when leaving HTML for chat,
	// otherwise the re-render overwrites it.
	bool oldHtml = pm.type != MenuType::Chat;
	pm.type = ResolveType(*newDef, slot);
	bool newHtml = pm.type != MenuType::Chat;
	if (oldHtml && !newHtml)
	{
		center_html::Send(slot, kHtmlClearContent, kHtmlClearDurationSecs);
	}

	pm.handle = handle;
	pm.cursor = newDef->startItem;
	pm.page = newDef->startItem / m_itemsPerPage; // m_itemsPerPage clamped >= 1 in Configure
	// Re-baseline buttons so the key that triggered the switch doesn't act again in the new menu.
	pm.prevButtons = 0;
	pm.buttonsPrimed = false;
	pm.nextHtmlRender = 0.0f;
	pm.lastHtml.clear();
	// expireTime is kept so the whole submenu stack shares one timeout.

	Render(slot);
}

bool MenuManager::ProcessInput(int slot, const char *text, float curtime)
{
	ScopedLock lock(m_mutex);
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
	if (pm.type != MenuType::Chat)
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
	return ApplyChatNumber(slot, num);
}

bool MenuManager::ApplyChatNumber(int slot, int num)
{
	PlayerMenu &pm = m_players[slot];
	MenuDef *def = Find(pm.handle);
	if (!def || pm.type != MenuType::Chat)
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

	// Key layout: items 1..pageItems, Next = itemsPerPage+1, Prev = +2, Exit/Back = 0.
	if (num == 0)
	{
		// In a submenu, 0 steps back to the parent. Otherwise it exits.
		if (def->parent != kInvalidMenuHandle && Find(def->parent))
		{
			SwitchMenu(slot, def->parent);
			return true;
		}
		if (def->exitButton)
		{
			EndDisplay(slot, MenuEndReason::Exit);
			return true;
		}
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
	ScopedLock lock(m_mutex);
	if (slot < 0 || slot > MAXPLAYERS || !m_players[slot].active)
	{
		return false;
	}
	// Use the resolved per-viewer type, not the menu's base type.
	return m_players[slot].type != MenuType::Chat;
}

bool MenuManager::AnyHtmlMenuActive() const
{
	ScopedLock lock(m_mutex);
	for (int i = 0; i <= MAXPLAYERS; i++)
	{
		if (m_players[i].active && m_players[i].type != MenuType::Chat)
		{
			return true;
		}
	}
	return false;
}

void MenuManager::PollButtons(int slot, uint64_t heldButtons, float curtime)
{
	ScopedLock lock(m_mutex);
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
	if (!def || pm.type == MenuType::Chat)
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

	if (newly & EffectiveNavMask(*def, slot, MenuNavAction::Up))
	{
		HtmlMoveCursor(slot, -1);
	}
	else if (newly & EffectiveNavMask(*def, slot, MenuNavAction::Down))
	{
		HtmlMoveCursor(slot, +1);
	}
	else if (newly & EffectiveNavMask(*def, slot, MenuNavAction::Select))
	{
		HtmlNavSelect(slot);
	}
	else if (newly & EffectiveNavMask(*def, slot, MenuNavAction::Back))
	{
		NavClose(slot);
	}
}

void MenuManager::HtmlNavSelect(int slot)
{
	PlayerMenu &pm = m_players[slot];
	MenuDef *def = Find(pm.handle);
	if (!def)
	{
		return;
	}
	// Selecting the inline Exit row closes the menu, otherwise pick the cursor item.
	if (HtmlShowsExitRow(*def, slot) && pm.cursor == static_cast<int>(def->items.size()))
	{
		EndDisplay(slot, MenuEndReason::Exit);
	}
	else
	{
		Select(slot, pm.cursor);
	}
}

void MenuManager::NavClose(int slot)
{
	PlayerMenu &pm = m_players[slot];
	MenuDef *def = Find(pm.handle);
	if (!def)
	{
		return;
	}
	// Step up to the parent in a submenu, else exit (if exitable).
	if (def->parent != kInvalidMenuHandle && Find(def->parent))
	{
		SwitchMenu(slot, def->parent);
	}
	else if (def->exitButton)
	{
		EndDisplay(slot, MenuEndReason::Exit);
	}
}

void MenuManager::CommandNav(int slot, MenuNavAction action, float curtime)
{
	ScopedLock lock(m_mutex);
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
	if (!def)
	{
		return;
	}
	m_curtime = curtime;

	// Up/Down/Select drive the HTML cursor (chat menus use typed numbers).
	// Back/close works for both render types.
	bool html = (pm.type != MenuType::Chat);
	switch (action)
	{
		case MenuNavAction::Up:
			if (html)
			{
				HtmlMoveCursor(slot, -1);
			}
			break;
		case MenuNavAction::Down:
			if (html)
			{
				HtmlMoveCursor(slot, +1);
			}
			break;
		case MenuNavAction::Select:
			if (html)
			{
				HtmlNavSelect(slot);
			}
			break;
		case MenuNavAction::Back:
			NavClose(slot);
			break;
	}
}

void MenuManager::CommandSelectNumber(int slot, int number, float curtime)
{
	ScopedLock lock(m_mutex);
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
	if (!def)
	{
		return;
	}
	m_curtime = curtime;

	// Numbers map to chat-menu entries (clamped to the current page).
	// HTML menus show no numbers, so the number is ignored and the cursor row is selected instead.
	if (pm.type == MenuType::Chat)
	{
		ApplyChatNumber(slot, number);
	}
	else
	{
		HtmlNavSelect(slot);
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

	int count = HtmlRowCount(*def, slot); // includes the inline Exit row, if any
	if (count <= 0)
	{
		return;
	}
	// Wrap around, so a single bound key can cycle through every row.
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
	ScopedLock lock(m_mutex);
	m_curtime = curtime;

	// Drain off-thread work on main (m_curtime now set).
	// Swap first so a closure that queues more defers it to the next tick instead of looping.
	if (!m_pending.empty())
	{
		std::vector<std::function<void()>> pending;
		pending.swap(m_pending);
		for (auto &fn : pending)
		{
			fn();
		}
	}

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
		if (def && pm.type != MenuType::Chat && curtime >= pm.nextHtmlRender)
		{
			RenderHtml(i);
		}
	}
}

void MenuManager::OnPlayerDisconnect(int slot)
{
	ScopedLock lock(m_mutex);
	if (slot < 0 || slot > MAXPLAYERS)
	{
		return;
	}
	EndDisplay(slot, MenuEndReason::Disconnect);
	// Drop busy state so a reconnecting client on this slot starts clean.
	m_players[slot].externalBusy = false;
}

void MenuManager::SetExternalBusy(int slot, bool busy)
{
	ScopedLock lock(m_mutex);
	if (slot < 0 || slot > MAXPLAYERS)
	{
		return;
	}
	m_players[slot].externalBusy = busy;
	if (!busy || !m_players[slot].active)
	{
		return;
	}
	// A host menu just took the slot: drop ours.
	// EndDisplay (HTML clear + callback) is main-thread only, so defer if we're off-thread.
	if (!OnMainThread())
	{
		m_pending.push_back([this, slot] { EndDisplay(slot, MenuEndReason::Cancelled); });
		return;
	}
	EndDisplay(slot, MenuEndReason::Cancelled);
}

bool MenuManager::GetExternalBusy(int slot) const
{
	ScopedLock lock(m_mutex);
	if (slot < 0 || slot > MAXPLAYERS)
	{
		return false;
	}
	return m_players[slot].externalBusy;
}

void MenuManager::Shutdown()
{
	ScopedLock lock(m_mutex);
	m_pending.clear();
	for (int i = 0; i <= MAXPLAYERS; i++)
	{
		m_players[i].active = false;
		m_players[i].handle = kInvalidMenuHandle;
	}
	m_menus.clear();
}

void MenuManager::Configure(const MenuManagerSettings &settings)
{
	ScopedLock lock(m_mutex);
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
	ScopedLock lock(m_mutex);
	m_htmlAvailable = available;
}

void MenuManager::SetLanguageResolver(std::function<std::string(int)> resolver)
{
	ScopedLock lock(m_mutex);
	m_langResolver = std::move(resolver);
}

bool MenuManager::HasHtml() const
{
	ScopedLock lock(m_mutex);
	return m_htmlAvailable;
}

// A live nav-pref change re-renders the player's open menu so footer key hints update at once.
// Render touches the engine, so it only runs on the main thread,
// off-thread the pref still applies the next time the menu renders.
void MenuManager::SetPlayerTypePref(int slot, MenuType type)
{
	ScopedLock lock(m_mutex);
	if (slot < 0 || slot > MAXPLAYERS)
	{
		return;
	}
	m_prefs[slot].type = type;
	// Type changes take effect the next time a menu is opened, not on the current display
	// (switching the chat/HTML channel mid-display would be jarring).
}

void MenuManager::SetPlayerNavPref(int slot, MenuNavAction action, uint64_t mask, const char *label)
{
	ScopedLock lock(m_mutex);
	if (slot < 0 || slot > MAXPLAYERS)
	{
		return;
	}
	NavOverride &nav = m_prefs[slot].nav[static_cast<int>(action)];
	nav.mask = mask;
	nav.label = label ? label : "";
	if (m_players[slot].active && OnMainThread())
	{
		Render(slot);
	}
}

void MenuManager::SetPlayerNavDisabled(int slot, MenuNavAction action)
{
	ScopedLock lock(m_mutex);
	if (slot < 0 || slot > MAXPLAYERS)
	{
		return;
	}
	NavOverride &nav = m_prefs[slot].nav[static_cast<int>(action)];
	nav.mask = kNavDisabledSentinel;
	nav.label = "";
	if (m_players[slot].active && OnMainThread())
	{
		Render(slot);
	}
}

void MenuManager::ClearPlayerNavPref(int slot, MenuNavAction action)
{
	ScopedLock lock(m_mutex);
	if (slot < 0 || slot > MAXPLAYERS)
	{
		return;
	}
	NavOverride &nav = m_prefs[slot].nav[static_cast<int>(action)];
	nav.mask = 0;
	nav.label = "";
	if (m_players[slot].active && OnMainThread())
	{
		Render(slot);
	}
}

void MenuManager::ClearPlayerPrefs(int slot)
{
	ScopedLock lock(m_mutex);
	if (slot < 0 || slot > MAXPLAYERS)
	{
		return;
	}
	m_prefs[slot] = PlayerPrefs {};
}

void MenuManager::RefreshMenu(MenuHandle menu)
{
	// Rendering touches the engine, defer off-thread callers to GameFrame.
	if (!OnMainThread())
	{
		m_pending.push_back([this, menu] { RefreshMenu(menu); });
		return;
	}
	for (int slot = 0; slot <= MAXPLAYERS; slot++)
	{
		PlayerMenu &pm = m_players[slot];
		if (pm.active && pm.handle == menu)
		{
			Render(slot);
		}
	}
}

void MenuManager::RunOnMainThread(std::function<void()> fn)
{
	ScopedLock lock(m_mutex);
	if (OnMainThread())
	{
		fn();
	}
	else
	{
		m_pending.push_back(std::move(fn));
	}
}

void MenuManager::Render(int slot)
{
	const MenuDef *def = Find(m_players[slot].handle);
	if (!def)
	{
		return;
	}
	if (m_players[slot].type == MenuType::Chat)
	{
		RenderPage(slot);
	}
	else
	{
		RenderHtml(slot);
	}
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
	// Clamp a page left stale by a live item removal / start-item past the end.
	if (pm.page >= pageCount)
	{
		pm.page = pageCount - 1;
	}

	bool hasMore = (pm.page + 1 < pageCount);
	bool hasPrev = (pm.page > 0);

	int pageStart = pm.page * m_itemsPerPage;
	int pageEnd = (std::min)(pageStart + m_itemsPerPage, static_cast<int>(items.size()));
	int pageItems = pageEnd - pageStart;

	const MenuManagerSettings &s = m_settings;

	// Optional branded header line above the title.
	if (!s.chatHeader.empty())
	{
		std::string header = s.chatHeaderColor + s.chatHeader;
		MENU_PrintToChat(slot, "%s", header.c_str());
	}

	// Built as std::string so long titles/items aren't truncated by a fixed buffer.
	std::string title = s.chatTitleColor + s.chatTitlePrefix + def->title;
	if (pageCount > 1 && s.chatShowPage)
	{
		title += " " + s.chatPageColor + s.chatPagePrefix + std::to_string(pm.page + 1) + "/" + std::to_string(pageCount) + s.chatPageSuffix;
	}
	title += s.chatTitleColor + s.chatTitleSuffix;
	MENU_PrintToChat(slot, "%s", title.c_str());

	// Item rows: color + number + text. The number is the on-screen 1..N selection key.
	for (int i = 0; i < pageItems; i++)
	{
		const MenuItem &item = items[pageStart + i];
		std::string line = item.disabled ? (s.chatDisabledColor + s.chatDisabledPrefix) : (s.chatItemColor + s.chatNumberPrefix);
		line += std::to_string(i + 1);
		line += s.chatNumberSuffix;
		line += item.text;
		MENU_PrintToChat(slot, "%s", line.c_str());
	}

	// Nav/exit rows: item-colored number, then arrow-colored arrow + label.
	auto navRow = [&](int key, MenuLabel label)
	{
		std::string line = s.chatItemColor + s.chatNumberPrefix + std::to_string(key) + s.chatNumberSuffix;
		line += s.chatArrowColor + s.chatArrow + ResolveLabel(slot, *def, label);
		MENU_PrintToChat(slot, "%s", line.c_str());
	};
	if (hasMore)
	{
		navRow(m_itemsPerPage + 1, MenuLabel::NextPage);
	}
	if (hasPrev)
	{
		navRow(m_itemsPerPage + 2, MenuLabel::PrevPage);
	}
	if (def->exitButton)
	{
		navRow(0, MenuLabel::Exit);
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

	pm.nextHtmlRender = m_curtime + m_settings.htmlRefreshInterval;

	const auto &items = def->items;
	int itemCount = static_cast<int>(items.size());
	bool exitRow = HtmlShowsExitRow(*def, slot);
	int count = itemCount + (exitRow ? 1 : 0); // navigable rows (Exit is the last)

	if (pm.cursor < 0)
	{
		pm.cursor = 0;
	}
	else if (count > 0 && pm.cursor >= count)
	{
		pm.cursor = count - 1;
	}

	// Resolve effective style: per-menu override, else the server default.
	const StyleOverride &st = def->style;
	const std::string &titleColor = st.titleColor.empty() ? m_settings.titleColor : st.titleColor;
	const std::string &navColor = st.navColor.empty() ? m_settings.navColor : st.navColor;
	const std::string &footerColor = st.footerColor.empty() ? m_settings.footerColor : st.footerColor;
	const std::string &disabledColor = st.disabledColor.empty() ? m_settings.disabledColor : st.disabledColor;
	const std::string &itemColor = st.itemColor.empty() ? m_settings.itemColor : st.itemColor;
	const std::string &fontFace = st.fontFace.empty() ? m_settings.fontFace : st.fontFace;
	const std::string &marker = st.marker.empty() ? m_settings.marker : st.marker;
	const std::string &counterColor = st.counterColor.empty() ? m_settings.counterColor : st.counterColor;
	const std::string &submenuSuffix = st.submenuSuffix.empty() ? m_settings.submenuSuffix : st.submenuSuffix;
	const std::string &footerSep = st.footerSeparator.empty() ? m_settings.footerSeparator : st.footerSeparator;
	const std::string &counterPrefix = st.counterPrefix.empty() ? m_settings.counterPrefix : st.counterPrefix;
	const std::string &counterSuffix = st.counterSuffix.empty() ? m_settings.counterSuffix : st.counterSuffix;
	const std::string &align = st.align.empty() ? m_settings.align : st.align;
	bool showCounter = (st.showCounter < 0) ? m_settings.showCounter : (st.showCounter != 0);
	bool showFooter = (st.showFooter < 0) ? m_settings.showFooter : (st.showFooter != 0);
	bool highlightText = (st.highlightText < 0) ? m_settings.highlightText : (st.highlightText != 0);
	// Suffix appended to every class list: alignment, then an optional font face.
	std::string commonCls = AlignClass(align);
	if (!fontFace.empty())
	{
		commonCls += " " + fontFace;
	}
	const std::string titleCls = SizeClass(st.titleSize.empty() ? m_settings.titleSize : st.titleSize) + commonCls;
	const std::string itemCls = SizeClass(st.itemSize.empty() ? m_settings.itemSize : st.itemSize) + commonCls;
	const std::string footerCls = SizeClass(st.footerSize.empty() ? m_settings.footerSize : st.footerSize) + commonCls;
	const std::string markerHtml = center_html::Escape(marker);

	std::string html;
	html.reserve(512);

	// Title + position counter (counter dimmed so the title reads first).
	html += center_html::ColorizeChat(def->title, titleColor.c_str(), titleCls.c_str());
	if (showCounter && count > 0)
	{
		html += " <font class='fontSize-s" + commonCls + "' color='" + counterColor + "'>";
		html += center_html::Escape(counterPrefix);
		html += std::to_string(pm.cursor + 1) + "/" + std::to_string(count);
		html += center_html::Escape(counterSuffix);
		html += "</font>";
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
				html += navColor;
				html += "' class='" + itemCls + "'>";
				html += markerHtml;
				html += "</font>";
			}
			html += "<font color='";
			html += (selected && highlightText) ? navColor : footerColor;
			html += "' class='" + itemCls + "'>";
			html += center_html::Escape(ResolveLabel(slot, *def, MenuLabel::Exit));
			html += "</font><br>";
			continue;
		}

		const MenuItem &item = items[i];

		if (selected)
		{
			html += "<font color='";
			html += navColor;
			html += "' class='" + itemCls + "'>";
			html += markerHtml;
			html += "</font>";
		}

		if (!item.iconUrl.empty())
		{
			html += "<img src='" + center_html::Escape(item.iconUrl) + "'> ";
		}

		const char *base = item.disabled ? disabledColor.c_str() : ((selected && highlightText) ? navColor.c_str() : itemColor.c_str());
		if (item.raw)
		{
			// Raw markup: keep the row's size/face/color wrapper but emit the text verbatim,
			// so it can embed <img>/<font>/etc. The consumer owns well-formedness.
			html += "<font color='";
			html += base;
			html += "' class='" + itemCls + "'>";
			html += item.text;
			html += "</font>";
		}
		else
		{
			html += center_html::ColorizeChat(item.text, base, itemCls.c_str());
		}
		// Trailing affordance so a submenu item reads as "opens another menu".
		if (item.submenu != kInvalidMenuHandle && !submenuSuffix.empty())
		{
			html += "<font color='";
			html += base;
			html += "' class='" + itemCls + "'>";
			html += center_html::Escape(submenuSuffix);
			html += "</font>";
		}
		html += "<br>";
	}

	// Footer key hints, adapting when a direction is disabled
	// (a single-key scroll shows "Scroll: KEY").
	bool upOn = EffectiveNavMask(*def, slot, MenuNavAction::Up) != 0;
	bool downOn = EffectiveNavMask(*def, slot, MenuNavAction::Down) != 0;
	bool selectOn = EffectiveNavMask(*def, slot, MenuNavAction::Select) != 0;
	bool backOn = def->exitButton && EffectiveNavMask(*def, slot, MenuNavAction::Back) != 0;

	std::string footerSepEsc = center_html::Escape(footerSep);
	std::string footer;
	auto addSegment = [&footer, &footerSepEsc](const std::string &seg)
	{
		if (seg.empty())
		{
			return;
		}
		if (!footer.empty())
		{
			footer += footerSepEsc;
		}
		footer += seg;
	};

	if (upOn && downOn)
	{
		addSegment(center_html::Escape(ResolveLabel(slot, *def, MenuLabel::Move)) + ": " + EffectiveNavLabel(*def, slot, MenuNavAction::Up) + "/"
				   + EffectiveNavLabel(*def, slot, MenuNavAction::Down));
	}
	else if (downOn)
	{
		addSegment(center_html::Escape(ResolveLabel(slot, *def, MenuLabel::Scroll)) + ": " + EffectiveNavLabel(*def, slot, MenuNavAction::Down));
	}
	else if (upOn)
	{
		addSegment(center_html::Escape(ResolveLabel(slot, *def, MenuLabel::Scroll)) + ": " + EffectiveNavLabel(*def, slot, MenuNavAction::Up));
	}
	if (selectOn)
	{
		addSegment(center_html::Escape(ResolveLabel(slot, *def, MenuLabel::Select)) + ": " + EffectiveNavLabel(*def, slot, MenuNavAction::Select));
	}
	if (backOn)
	{
		addSegment(center_html::Escape(ResolveLabel(slot, *def, MenuLabel::Exit)) + ": " + EffectiveNavLabel(*def, slot, MenuNavAction::Back));
	}

	if (showFooter && !footer.empty())
	{
		html += "<font color='";
		html += footerColor;
		html += "' class='" + footerCls + "'>";
		html += footer;
		html += "</font>";
	}

	// Skip the network send when nothing changed, except a periodic keep-alive so the
	// decaying panel doesn't blink. Saves bandwidth with many viewers idling on a menu.
	bool changed = (html != pm.lastHtml);
	bool keepAliveDue = (m_curtime - pm.lastHtmlSend) >= m_settings.htmlKeepAlive;
	if (changed || keepAliveDue)
	{
		center_html::Send(slot, html.c_str(), m_settings.htmlDurationSecs);
		pm.lastHtml = html;
		pm.lastHtmlSend = m_curtime;
	}
}
