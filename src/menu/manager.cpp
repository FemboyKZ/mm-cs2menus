#include "manager.h"
#include "src/lang/translations.h"
#include "src/menu/key_table.h"
#include "src/render/center_html.h"
#include "src/utils/html_style.h"
#include "src/utils/print.h"

#include <cmath>
#include <algorithm>
#include <cctype>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <map>
#include <utility>

// Recursive so a callback can re-enter the API on the same thread.
using ScopedLock = std::lock_guard<std::recursive_mutex>;

// Per-menu override value meaning "explicitly disabled" (MenuButton::None),
// as opposed to mask 0 which means "inherit the server config binding".
// All bits set is never a real single-button IN_* mask.
static constexpr uint64_t kNavDisabledSentinel = ~0ull;

// Map a public MenuButton to its IN_* mask + footer label (see keys::kKeys).
// Default (and any unknown) -> {0, ""} (inherit); None -> {sentinel, ""} (disabled).
static void MenuButtonToBinding(MenuButton button, uint64_t &outMask, const char *&outLabel)
{
	if (button == MenuButton::None)
	{
		outMask = kNavDisabledSentinel;
		outLabel = "";
		return;
	}
	if (const keys::KeyDef *k = keys::FindByButton(button))
	{
		outMask = k->mask;
		outLabel = k->label;
		return;
	}
	outMask = 0; // Default / unknown: inherit the server config binding
	outLabel = "";
}

// Map an HTML size token to its Panorama fontSize class (see html_style::IsSizeToken).
// An already-qualified "fontSize-..." string passes through. Unknown -> "".
static std::string SizeClass(const std::string &tok)
{
	if (tok.rfind("fontSize-", 0) == 0)
	{
		return tok;
	}
	if (html_style::IsSizeToken(tok))
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

// Empty clears a per-menu flag override, "0" is off and anything else on.
static int ParseTriState(const std::string &v)
{
	return v.empty() ? -1 : (v != "0" ? 1 : 0);
}

// Empty clears the override. An unknown token is ignored so a typo can't blank the size.
static bool IsSizeOrEmpty(const std::string &v)
{
	return v.empty() || !SizeClass(v).empty();
}

// The per-menu override when set, else the server setting.
static const char *Pick(const std::string &over, const std::string &fallback)
{
	return over.empty() ? fallback.c_str() : over.c_str();
}

static const char *PickBool(int over, bool fallback)
{
	return (over < 0 ? fallback : over != 0) ? "1" : "0";
}

// Substitute "{name}" placeholders in a template string. Unknown tokens are left untouched,
// and a value is never re-scanned, so a value containing "{...}" can't trigger further replacement.
static std::string FillTemplate(const std::string &tmpl, std::initializer_list<std::pair<const char *, std::string>> vars)
{
	std::string out = tmpl;
	for (const auto &kv : vars)
	{
		const std::string token = std::string("{") + kv.first + "}";
		size_t pos = out.find(token);
		while (pos != std::string::npos)
		{
			out.replace(pos, token.size(), kv.second);
			pos = out.find(token, pos + kv.second.size());
		}
	}
	return out;
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
	// PlayerMenu::editItem while the list popup holds every tab, and the "+N" tab among PlayerMenu::panoramaNav.
	constexpr int kEditTabs = -100;
	constexpr int kNavMore = -2;

	// kMenuTextIndex: shorter lists get no index.
	constexpr int kIndexMinPages = 4;

	// What a tab row holds, in pixels as the layouts' styles lay it out. The server can't measure text, so a tab's width
	// is guessed from its characters, on the wide side: one that would have fit going behind "+N" beats one cut off.
	constexpr int kTabCell = 10;     // a 15px uppercase character
	constexpr int kTabPadding = 30;  // a tab's padding and margin
	constexpr int kTabDot = 13;      // a marked tab's dot
	constexpr int kTabMore = 76;     // "+N" with its caret
	constexpr int kTabPager = 130;   // the page arrows at the row's end
	constexpr int kChipCell = 8;     // a 13px bold character
	constexpr int kChipPadding = 32; // a chip's padding, border and margin
	constexpr int kChipCaret = 16;
	constexpr int kTabRow = 860; // the 908 wide box's inner width

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

MenuManager::MenuItem *MenuManager::FindItem(MenuHandle menu, int item)
{
	MenuDef *def = Find(menu);
	if (!def || item < 0 || item >= static_cast<int>(def->items.size()))
	{
		return nullptr;
	}
	return &def->items[item];
}

const MenuManager::MenuItem *MenuManager::FindItem(MenuHandle menu, int item) const
{
	const MenuDef *def = Find(menu);
	if (!def || item < 0 || item >= static_cast<int>(def->items.size()))
	{
		return nullptr;
	}
	return &def->items[item];
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
	MenuItem item;
	item.text = text ? text : "";
	item.info = info ? info : "";
	item.disabled = disabled;
	item.section = static_cast<int>(def->sections.size()) - 1;
	def->items.push_back(std::move(item));
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

	MenuItem item;
	item.text = text ? text : "";
	item.info = info ? info : "";
	item.submenu = child;
	item.section = static_cast<int>(parentDef->sections.size()) - 1;
	parentDef->items.push_back(std::move(item));
	return static_cast<int>(parentDef->items.size()) - 1;
}

void MenuManager::SetTitle(MenuHandle menu, const char *title)
{
	ScopedLock lock(m_mutex);
	if (MenuDef *def = Find(menu))
	{
		def->title = title ? title : "";
		// Like SetItemText, so a menu kept open by CloseOnSelect off can show live values in its title.
		RefreshMenu(menu);
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

void MenuManager::SetMenuRefreshCallback(MenuHandle menu, MenuRefreshFn onRefresh)
{
	ScopedLock lock(m_mutex);
	if (MenuDef *def = Find(menu))
	{
		def->onRefresh = std::move(onRefresh);
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
		// Global / layout
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
		case MenuStyle::FontFace:
			s.fontFace = v;
			break;
		case MenuStyle::VisibleItems:
			// Empty clears the override. A positive integer sets the scroll window (clamped at render).
			if (v.empty())
			{
				s.visibleItems = -1;
			}
			else if (int n = atoi(v.c_str()); n > 0)
			{
				s.visibleItems = n;
			}
			break;
		// Title
		case MenuStyle::TitleColor:
			s.titleColor = v;
			break;
		case MenuStyle::TitleSize:
			if (IsSizeOrEmpty(v))
			{
				s.titleSize = v;
			}
			break;
		case MenuStyle::RawTitle:
			s.rawTitle = ParseTriState(v);
			break;
		// Items
		case MenuStyle::ItemColor:
			s.itemColor = v;
			break;
		case MenuStyle::ItemSize:
			if (IsSizeOrEmpty(v))
			{
				s.itemSize = v;
			}
			break;
		case MenuStyle::DisabledColor:
			s.disabledColor = v;
			break;
		case MenuStyle::SubmenuSuffix:
			s.submenuSuffix = v;
			break;
		// Cursor row
		case MenuStyle::NavColor:
			s.navColor = v;
			break;
		case MenuStyle::Marker:
			s.marker = v;
			break;
		case MenuStyle::HighlightText:
			s.highlightText = ParseTriState(v);
			break;
		// Position counter
		case MenuStyle::ShowCounter:
			s.showCounter = ParseTriState(v);
			break;
		case MenuStyle::CounterColor:
			s.counterColor = v;
			break;
		case MenuStyle::CounterSize:
			if (IsSizeOrEmpty(v))
			{
				s.counterSize = v;
			}
			break;
		case MenuStyle::CounterFormat:
			s.counterFormat = v;
			break;
		// Key-hint footer
		case MenuStyle::ShowFooter:
			s.showFooter = ParseTriState(v);
			break;
		case MenuStyle::FooterColor:
			s.footerColor = v;
			break;
		case MenuStyle::FooterSize:
			if (IsSizeOrEmpty(v))
			{
				s.footerSize = v;
			}
			break;
		case MenuStyle::FooterSeparator:
			s.footerSeparator = v;
			break;
		case MenuStyle::FooterHintFormat:
			s.footerHintFormat = v;
			break;
		case MenuStyle::FooterRangeFormat:
			s.footerRangeFormat = v;
			break;
		// Panorama
		case MenuStyle::PagePrefixDelimiter:
			s.pagePrefixDelimiter = v.size() == 1 ? v[0] : 0;
			break;
		// Value items
		case MenuStyle::ValueFormat:
			s.valueFormat = v;
			break;
		case MenuStyle::EditFormat:
			s.editFormat = v;
			break;
		// Sections
		case MenuStyle::SectionFormat:
			s.sectionFormat = v;
			break;
		case MenuStyle::SectionColor:
			s.sectionColor = v;
			break;
	}
	RefreshMenu(menu);
}

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
		// Global / layout
		case MenuStyle::Align:
			return Pick(s.align, m_settings.align);
		case MenuStyle::FontFace:
			return Pick(s.fontFace, m_settings.fontFace);
		case MenuStyle::VisibleItems:
		{
			int eff = (s.visibleItems > 0) ? (std::min)(s.visibleItems, MENU_MAX_HTML_VISIBLE) : m_htmlVisibleItems;
			static thread_local std::string buf;
			buf = std::to_string(eff);
			return buf.c_str();
		}
		// Title
		case MenuStyle::TitleColor:
			return Pick(s.titleColor, m_settings.titleColor);
		case MenuStyle::TitleSize:
			return Pick(s.titleSize, m_settings.titleSize);
		case MenuStyle::RawTitle:
			return (s.rawTitle == 1) ? "1" : "0";
		// Items
		case MenuStyle::ItemColor:
			return Pick(s.itemColor, m_settings.itemColor);
		case MenuStyle::ItemSize:
			return Pick(s.itemSize, m_settings.itemSize);
		case MenuStyle::DisabledColor:
			return Pick(s.disabledColor, m_settings.disabledColor);
		case MenuStyle::SubmenuSuffix:
			return Pick(s.submenuSuffix, m_settings.submenuSuffix);
		// Cursor row
		case MenuStyle::NavColor:
			return Pick(s.navColor, m_settings.navColor);
		case MenuStyle::Marker:
			return Pick(s.marker, m_settings.marker);
		case MenuStyle::HighlightText:
			return PickBool(s.highlightText, m_settings.highlightText);
		// Position counter
		case MenuStyle::ShowCounter:
			return PickBool(s.showCounter, m_settings.showCounter);
		case MenuStyle::CounterColor:
			return Pick(s.counterColor, m_settings.counterColor);
		case MenuStyle::CounterSize:
			return Pick(s.counterSize, m_settings.counterSize);
		case MenuStyle::CounterFormat:
			return Pick(s.counterFormat, m_settings.counterFormat);
		// Key-hint footer
		case MenuStyle::ShowFooter:
			return PickBool(s.showFooter, m_settings.showFooter);
		case MenuStyle::FooterColor:
			return Pick(s.footerColor, m_settings.footerColor);
		case MenuStyle::FooterSize:
			return Pick(s.footerSize, m_settings.footerSize);
		case MenuStyle::FooterSeparator:
			return Pick(s.footerSeparator, m_settings.footerSeparator);
		case MenuStyle::FooterHintFormat:
			return Pick(s.footerHintFormat, m_settings.footerHintFormat);
		case MenuStyle::FooterRangeFormat:
			return Pick(s.footerRangeFormat, m_settings.footerRangeFormat);
		// Panorama
		case MenuStyle::PagePrefixDelimiter:
		{
			static thread_local std::string buf;
			buf = s.pagePrefixDelimiter ? std::string(1, s.pagePrefixDelimiter) : std::string();
			return buf.c_str();
		}
		// Value items
		case MenuStyle::ValueFormat:
			return Pick(s.valueFormat, m_settings.valueFormat);
		case MenuStyle::EditFormat:
			return Pick(s.editFormat, m_settings.editFormat);
		// Sections
		case MenuStyle::SectionFormat:
			return Pick(s.sectionFormat, m_settings.sectionFormat);
		case MenuStyle::SectionColor:
			return Pick(s.sectionColor, m_settings.sectionColor);
	}
	return "";
}

void MenuManager::SetItemText(MenuHandle menu, int item, const char *text)
{
	ScopedLock lock(m_mutex);
	if (MenuItem *it = FindItem(menu, item))
	{
		it->text = text ? text : "";
		RefreshMenu(menu);
	}
}

void MenuManager::SetItemInfo(MenuHandle menu, int item, const char *info)
{
	ScopedLock lock(m_mutex);
	if (MenuItem *it = FindItem(menu, item))
	{
		it->info = info ? info : ""; // info isn't rendered, no refresh
	}
}

bool MenuManager::GetItemDisabled(MenuHandle menu, int item) const
{
	ScopedLock lock(m_mutex);
	const MenuItem *it = FindItem(menu, item);
	return it ? it->disabled : false;
}

void MenuManager::SetItemDisabled(MenuHandle menu, int item, bool disabled)
{
	ScopedLock lock(m_mutex);
	if (MenuItem *it = FindItem(menu, item))
	{
		it->disabled = disabled;
		RefreshMenu(menu);
	}
}

bool MenuManager::GetItemRaw(MenuHandle menu, int item) const
{
	ScopedLock lock(m_mutex);
	const MenuItem *it = FindItem(menu, item);
	return it ? it->raw : false;
}

void MenuManager::SetItemRaw(MenuHandle menu, int item, bool raw)
{
	ScopedLock lock(m_mutex);
	if (MenuItem *it = FindItem(menu, item))
	{
		it->raw = raw;
		RefreshMenu(menu);
	}
}

const char *MenuManager::GetItemIcon(MenuHandle menu, int item) const
{
	ScopedLock lock(m_mutex);
	const MenuItem *it = FindItem(menu, item);
	return it ? it->iconUrl.c_str() : "";
}

void MenuManager::SetItemIcon(MenuHandle menu, int item, const char *url)
{
	ScopedLock lock(m_mutex);
	if (MenuItem *it = FindItem(menu, item))
	{
		it->iconUrl = url ? url : "";
		RefreshMenu(menu);
	}
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
	def->sections.clear();
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

const char *MenuManager::GetTitle(MenuHandle menu) const
{
	ScopedLock lock(m_mutex);
	const MenuDef *def = Find(menu);
	return def ? def->title.c_str() : "";
}

bool MenuManager::IsValidMenu(MenuHandle menu) const
{
	ScopedLock lock(m_mutex);
	return Find(menu) != nullptr;
}

int MenuManager::InsertItem(MenuHandle menu, int pos, const char *text, const char *info, bool disabled)
{
	ScopedLock lock(m_mutex);
	MenuDef *def = Find(menu);
	if (!def)
	{
		return -1;
	}
	int count = static_cast<int>(def->items.size());
	pos = (std::max)(0, (std::min)(pos, count));
	MenuItem item;
	item.text = text ? text : "";
	item.info = info ? info : "";
	item.disabled = disabled;
	// Sections stay contiguous: it joins the item before it.
	if (pos > 0)
	{
		item.section = def->items[pos - 1].section;
	}
	else if (!def->items.empty())
	{
		item.section = def->items[0].section;
	}
	def->items.insert(def->items.begin() + pos, std::move(item));
	RefreshMenu(menu); // RenderHtml/RenderPage clamp any now-stale cursor/page
	return pos;
}

MenuHandle MenuManager::GetItemSubmenu(MenuHandle menu, int item) const
{
	ScopedLock lock(m_mutex);
	const MenuItem *it = FindItem(menu, item);
	return it ? it->submenu : kInvalidMenuHandle;
}

void MenuManager::SetItemSubmenu(MenuHandle menu, int item, MenuHandle child)
{
	ScopedLock lock(m_mutex);
	if (child == menu)
	{
		return;
	}
	MenuItem *it = FindItem(menu, item);
	if (!it)
	{
		return;
	}
	if (child != kInvalidMenuHandle)
	{
		if (!Find(child))
		{
			return; // unknown child handle: leave the item untouched
		}
	}
	it->submenu = child;
	RefreshMenu(menu);
}

int MenuManager::AddValueItem(MenuHandle menu, MenuItem item)
{
	MenuDef *def = Find(menu);
	if (!def)
	{
		return -1;
	}
	item.value = ClampValue(item, item.value);
	item.section = static_cast<int>(def->sections.size()) - 1;
	def->items.push_back(std::move(item));
	return static_cast<int>(def->items.size()) - 1;
}

int MenuManager::AddToggle(MenuHandle menu, const char *text, bool on, const char *info)
{
	ScopedLock lock(m_mutex);
	MenuItem item;
	item.text = text ? text : "";
	item.info = info ? info : "";
	item.type = MenuItemType::Toggle;
	item.value = on ? 1 : 0;
	return AddValueItem(menu, std::move(item));
}

int MenuManager::AddStepper(MenuHandle menu, const char *text, int value, int min, int max, int step, const char *info)
{
	ScopedLock lock(m_mutex);
	MenuItem item;
	item.text = text ? text : "";
	item.info = info ? info : "";
	item.type = MenuItemType::Stepper;
	item.value = value;
	item.min = (std::min)(min, max);
	item.max = (std::max)(min, max);
	item.step = (std::max)(1, step);
	return AddValueItem(menu, std::move(item));
}

int MenuManager::AddChoice(MenuHandle menu, const char *text, const char *const *options, int optionCount, int selected, const char *info)
{
	ScopedLock lock(m_mutex);
	MenuItem item;
	item.text = text ? text : "";
	item.info = info ? info : "";
	item.type = MenuItemType::Choice;
	item.value = selected;
	for (int i = 0; options && i < optionCount; i++)
	{
		item.options.emplace_back(options[i] ? options[i] : "");
	}
	return AddValueItem(menu, std::move(item));
}

MenuItemType MenuManager::GetItemType(MenuHandle menu, int item) const
{
	ScopedLock lock(m_mutex);
	const MenuItem *it = FindItem(menu, item);
	return it ? it->type : MenuItemType::Normal;
}

void MenuManager::SetItemValue(MenuHandle menu, int item, int value)
{
	ScopedLock lock(m_mutex);
	if (MenuItem *it = FindItem(menu, item))
	{
		it->value = ClampValue(*it, value);
		RefreshMenu(menu);
	}
}

int MenuManager::GetItemValue(MenuHandle menu, int item) const
{
	ScopedLock lock(m_mutex);
	const MenuItem *it = FindItem(menu, item);
	return it ? it->value : 0;
}

void MenuManager::SetMenuChangeCallback(MenuHandle menu, MenuItemChangeFn onChange)
{
	ScopedLock lock(m_mutex);
	if (MenuDef *def = Find(menu))
	{
		def->onChange = std::move(onChange);
	}
}

int MenuManager::AddSection(MenuHandle menu, const char *name)
{
	ScopedLock lock(m_mutex);
	MenuDef *def = Find(menu);
	if (!def)
	{
		return -1;
	}
	def->sections.emplace_back(name ? name : "");
	return static_cast<int>(def->sections.size()) - 1;
}

int MenuManager::GetItemSection(MenuHandle menu, int item) const
{
	ScopedLock lock(m_mutex);
	const MenuItem *it = FindItem(menu, item);
	return it ? it->section : -1;
}

void MenuManager::SetMenuLayout(MenuHandle menu, MenuLayout layout)
{
	ScopedLock lock(m_mutex);
	if (MenuDef *def = Find(menu))
	{
		Repage(menu, *def, [&] { def->layout = layout; });
	}
}

MenuLayout MenuManager::GetMenuLayout(MenuHandle menu) const
{
	ScopedLock lock(m_mutex);
	const MenuDef *def = Find(menu);
	return def ? def->layout : MenuLayout::List;
}

void MenuManager::SetMenuTileSize(MenuHandle menu, MenuTileSize size)
{
	ScopedLock lock(m_mutex);
	if (MenuDef *def = Find(menu))
	{
		Repage(menu, *def, [&] { def->tileSize = size; });
	}
}

MenuTileSize MenuManager::GetMenuTileSize(MenuHandle menu) const
{
	ScopedLock lock(m_mutex);
	const MenuDef *def = Find(menu);
	return def ? def->tileSize : MenuTileSize::Small;
}

void MenuManager::SetMenuImage(MenuHandle menu, const char *image)
{
	ScopedLock lock(m_mutex);
	if (MenuDef *def = Find(menu))
	{
		def->image = image ? image : "";
		RefreshMenu(menu);
	}
}

const char *MenuManager::GetMenuImage(MenuHandle menu) const
{
	ScopedLock lock(m_mutex);
	const MenuDef *def = Find(menu);
	return def ? def->image.c_str() : "";
}

void MenuManager::SetMenuPinnedItem(MenuHandle menu, int item)
{
	ScopedLock lock(m_mutex);
	if (MenuDef *def = Find(menu))
	{
		Repage(menu, *def, [&] { def->pinned = item; });
	}
}

int MenuManager::GetMenuPinnedItem(MenuHandle menu) const
{
	ScopedLock lock(m_mutex);
	const MenuDef *def = Find(menu);
	return def ? def->pinned : -1;
}

int MenuManager::PinnedItem(const MenuDef &def, MenuType type) const
{
	const panorama_hud::Layout layout = type == MenuType::Panorama ? PanoramaLayout(def) : panorama_hud::Layout::List;
	const bool showcase = layout == panorama_hud::Layout::Showcase || layout == panorama_hud::Layout::Studio;
	const bool chat = type == MenuType::Chat && (def.textFeatures & kMenuTextPinned);
	return (showcase || chat) && def.pinned >= 0 && def.pinned < static_cast<int>(def.items.size()) ? def.pinned : -1;
}

void MenuManager::SetMenuSecondaryItem(MenuHandle menu, int item)
{
	ScopedLock lock(m_mutex);
	if (MenuDef *def = Find(menu))
	{
		Repage(menu, *def, [&] { def->secondary = item; });
	}
}

int MenuManager::SecondaryItem(const MenuDef &def, MenuType type) const
{
	const bool valid = def.secondary >= 0 && def.secondary < static_cast<int>(def.items.size()) && def.secondary != def.pinned;
	return valid && PinnedItem(def, type) >= 0 ? def.secondary : -1;
}

bool MenuManager::StudioImages(const MenuDef &def, int section)
{
	return std::any_of(def.items.begin(), def.items.end(),
					   [section](const MenuItem &item) { return !item.control && item.section == section && !item.image.empty(); });
}

std::vector<int> MenuManager::StudioControls(const MenuDef &def, const std::string &tab, std::vector<std::string> &tabs)
{
	auto sectionOf = [&def](const MenuItem &item)
	{ return item.section >= 0 && item.section < static_cast<int>(def.sections.size()) ? def.sections[item.section] : std::string(); };
	tabs.clear();
	for (const MenuItem &item : def.items)
	{
		if (item.control && std::find(tabs.begin(), tabs.end(), sectionOf(item)) == tabs.end())
		{
			tabs.push_back(sectionOf(item));
		}
	}
	const std::string shown = tabs.empty() || std::find(tabs.begin(), tabs.end(), tab) != tabs.end() ? tab : tabs.front();
	std::vector<int> items;
	for (int i = 0; i < static_cast<int>(def.items.size()); i++)
	{
		if (def.items[i].control && sectionOf(def.items[i]) == shown)
		{
			items.push_back(i);
		}
	}
	return items;
}

bool MenuManager::OffPage(const MenuDef &def, MenuType type, int item) const
{
	if (item == PinnedItem(def, type) || item == SecondaryItem(def, type))
	{
		return true;
	}
	if (type != MenuType::Panorama)
	{
		return false;
	}
	const panorama_hud::Layout layout = PanoramaLayout(def);
	return (def.items[item].control && layout == panorama_hud::Layout::Studio)
		   || (def.items[item].role == MenuItemRole::Input && layout != panorama_hud::Layout::List);
}

int MenuManager::PageItem(const MenuDef &def, MenuType type, const Page &page, int index) const
{
	for (int i = page.first; i < page.end && index >= 0; i++)
	{
		if (!OffPage(def, type, i) && index-- == 0)
		{
			return i;
		}
	}
	return -1;
}

void MenuManager::Repage(MenuHandle menu, MenuDef &def, const std::function<void()> &change)
{
	std::vector<std::pair<int, int>> viewers; // slot, first item shown
	for (int slot = 0; slot <= MAXPLAYERS; slot++)
	{
		const PlayerMenu &pm = m_players[slot];
		if (pm.active && pm.handle == menu)
		{
			const std::vector<Page> pages = Pages(def, pm.type);
			viewers.emplace_back(slot, pages[(std::max)(0, (std::min)(pm.page, static_cast<int>(pages.size()) - 1))].first);
		}
	}
	change();
	for (const auto &[slot, first] : viewers)
	{
		m_players[slot].page = PageOf(def, m_players[slot].type, first);
	}
	RefreshMenu(menu);
}

panorama_hud::TileSize MenuManager::TileSize(const MenuDef &def)
{
	// Longest run of items in one section, a section's items being consecutive.
	int longest = 0;
	for (int i = 0, run = 0; i < static_cast<int>(def.items.size()); i++)
	{
		run = i > 0 && def.items[i].section == def.items[i - 1].section ? run + 1 : 1;
		longest = (std::max)(longest, run);
	}
	const auto own = static_cast<panorama_hud::TileSize>(def.tileSize);
	for (auto size = panorama_hud::TileSize::Cards; size > own; size = static_cast<panorama_hud::TileSize>(static_cast<int>(size) - 1))
	{
		if (longest <= panorama_hud::TileSlots(size))
		{
			return size;
		}
	}
	return own;
}

void MenuManager::SetItemImage(MenuHandle menu, int item, const char *image)
{
	ScopedLock lock(m_mutex);
	if (MenuItem *it = FindItem(menu, item))
	{
		it->image = image ? image : "";
		RefreshMenu(menu);
	}
}

void MenuManager::SetItemControl(MenuHandle menu, int item, bool control)
{
	ScopedLock lock(m_mutex);
	MenuDef *def = Find(menu);
	MenuItem *it = def ? FindItem(menu, item) : nullptr;
	if (it && it->control != control)
	{
		Repage(menu, *def, [&] { it->control = control; });
	}
}

void MenuManager::SetItemRole(MenuHandle menu, int item, MenuItemRole role)
{
	ScopedLock lock(m_mutex);
	if (MenuItem *it = FindItem(menu, item))
	{
		it->role = role;
		RefreshMenu(menu);
	}
}

MenuItemRole MenuManager::GetItemRole(MenuHandle menu, int item) const
{
	ScopedLock lock(m_mutex);
	const MenuItem *it = FindItem(menu, item);
	return it ? it->role : MenuItemRole::Button;
}

void MenuManager::SetItemHighlight(MenuHandle menu, int item, bool highlight)
{
	ScopedLock lock(m_mutex);
	if (MenuItem *it = FindItem(menu, item))
	{
		it->highlight = highlight;
		RefreshMenu(menu);
	}
}

void MenuManager::SetItemSpan(MenuHandle menu, int item, int columns)
{
	ScopedLock lock(m_mutex);
	if (MenuItem *it = FindItem(menu, item))
	{
		it->span = std::clamp(columns, 1, 3);
		RefreshMenu(menu);
	}
}

void MenuManager::SetMenuInfo(MenuHandle menu, const char *title, const char *subtitle, const char *subtitleColor)
{
	ScopedLock lock(m_mutex);
	if (MenuDef *def = Find(menu))
	{
		def->info.title = title ? title : "";
		def->info.subtitle = subtitle ? subtitle : "";
		def->info.subtitleColor = subtitleColor ? subtitleColor : "";
		RefreshMenu(menu);
	}
}

void MenuManager::SetMenuInfoMeter(MenuHandle menu, float value, float rangeMin, float rangeMax, const float *bands, int bandCount, const char *label,
								   const char *valueText)
{
	ScopedLock lock(m_mutex);
	if (MenuDef *def = Find(menu))
	{
		MenuInfo &info = def->info;
		info.meter = value;
		info.rangeMin = (std::min)(rangeMin, rangeMax);
		info.rangeMax = (std::max)(rangeMin, rangeMax);
		info.bands.clear();
		if (bands && bandCount > 0)
		{
			info.bands.assign(bands, bands + bandCount);
		}
		info.meterLabel = label ? label : "";
		info.meterValue = valueText ? valueText : "";
		RefreshMenu(menu);
	}
}

int MenuManager::AddMenuInfoRow(MenuHandle menu, const char *label, const char *value)
{
	ScopedLock lock(m_mutex);
	MenuDef *def = Find(menu);
	if (!def)
	{
		return -1;
	}
	def->info.rows.emplace_back(label ? label : "", value ? value : "");
	RefreshMenu(menu);
	return static_cast<int>(def->info.rows.size()) - 1;
}

void MenuManager::ClearMenuInfo(MenuHandle menu)
{
	ScopedLock lock(m_mutex);
	if (MenuDef *def = Find(menu))
	{
		def->info = MenuInfo {};
		RefreshMenu(menu);
	}
}

const char *MenuManager::GetItemImage(MenuHandle menu, int item) const
{
	ScopedLock lock(m_mutex);
	const MenuItem *it = FindItem(menu, item);
	return it ? it->image.c_str() : "";
}

void MenuManager::SetItemSubtext(MenuHandle menu, int item, const char *subtext)
{
	ScopedLock lock(m_mutex);
	if (MenuItem *it = FindItem(menu, item))
	{
		it->subtext = subtext ? subtext : "";
		RefreshMenu(menu);
	}
}

const char *MenuManager::GetItemSubtext(MenuHandle menu, int item) const
{
	ScopedLock lock(m_mutex);
	const MenuItem *it = FindItem(menu, item);
	return it ? it->subtext.c_str() : "";
}

namespace
{
	// Lowercase letters, digits and dashes, so a rarity or tag can go into a class name.
	std::string ClassToken(const char *text)
	{
		std::string token;
		for (const char *c = text ? text : ""; *c; c++)
		{
			if (std::isalnum(static_cast<unsigned char>(*c)) || *c == '-')
			{
				token += static_cast<char>(std::tolower(static_cast<unsigned char>(*c)));
			}
		}
		return token;
	}
} // namespace

void MenuManager::SetItemRarity(MenuHandle menu, int item, const char *rarity)
{
	ScopedLock lock(m_mutex);
	if (MenuItem *it = FindItem(menu, item))
	{
		it->rarity = ClassToken(rarity);
		RefreshMenu(menu);
	}
}

void MenuManager::SetItemImageTint(MenuHandle menu, int item, const char *tint)
{
	ScopedLock lock(m_mutex);
	if (MenuItem *it = FindItem(menu, item))
	{
		it->imageTint = ClassToken(tint);
		RefreshMenu(menu);
	}
}

const char *MenuManager::GetItemRarity(MenuHandle menu, int item) const
{
	ScopedLock lock(m_mutex);
	const MenuItem *it = FindItem(menu, item);
	return it ? it->rarity.c_str() : "";
}

void MenuManager::SetItemTag(MenuHandle menu, int item, const char *tag, const char *style)
{
	ScopedLock lock(m_mutex);
	if (MenuItem *it = FindItem(menu, item))
	{
		it->tag = tag ? tag : "";
		it->tagStyle = ClassToken(style);
		RefreshMenu(menu);
	}
}

const char *MenuManager::GetItemTag(MenuHandle menu, int item) const
{
	ScopedLock lock(m_mutex);
	const MenuItem *it = FindItem(menu, item);
	return it ? it->tag.c_str() : "";
}

void MenuManager::SetItemTeams(MenuHandle menu, int item, int teams)
{
	ScopedLock lock(m_mutex);
	if (MenuItem *it = FindItem(menu, item))
	{
		it->teams = teams & (kMenuTeamT | kMenuTeamCT);
		RefreshMenu(menu);
	}
}

int MenuManager::GetItemTeams(MenuHandle menu, int item) const
{
	ScopedLock lock(m_mutex);
	const MenuItem *it = FindItem(menu, item);
	return it ? it->teams : 0;
}

void MenuManager::SetItemLocked(MenuHandle menu, int item, bool locked)
{
	ScopedLock lock(m_mutex);
	if (MenuItem *it = FindItem(menu, item))
	{
		it->locked = locked;
		RefreshMenu(menu);
	}
}

bool MenuManager::GetItemLocked(MenuHandle menu, int item) const
{
	ScopedLock lock(m_mutex);
	const MenuItem *it = FindItem(menu, item);
	return it && it->locked;
}

void MenuManager::SetItemCorner(MenuHandle menu, int item, MenuCorner corner)
{
	ScopedLock lock(m_mutex);
	if (MenuItem *it = FindItem(menu, item))
	{
		it->corner = corner;
		RefreshMenu(menu);
	}
}

MenuCorner MenuManager::GetItemCorner(MenuHandle menu, int item) const
{
	ScopedLock lock(m_mutex);
	const MenuItem *it = FindItem(menu, item);
	return it ? it->corner : MenuCorner::None;
}

void MenuManager::SetMenuCornerCallback(MenuHandle menu, MenuItemCornerFn onCorner)
{
	ScopedLock lock(m_mutex);
	if (MenuDef *def = Find(menu))
	{
		def->onCorner = std::move(onCorner);
	}
}

int MenuManager::AddMenuChip(MenuHandle menu, const char *label, const char *const *options, int optionCount, int selected, bool action)
{
	ScopedLock lock(m_mutex);
	MenuDef *def = Find(menu);
	if (!def || static_cast<int>(def->chips.size()) >= panorama_hud::kChipSlots)
	{
		return -1;
	}
	MenuDef::Chip chip;
	chip.label = label ? label : "";
	for (int i = 0; options && i < optionCount; i++)
	{
		chip.options.push_back(options[i] ? options[i] : "");
	}
	chip.action = action;
	// On or off for an action and a filter without options, else one of the options or none.
	const int optionsHeld = static_cast<int>(chip.options.size());
	chip.selected = action || chip.options.empty() ? (selected > 0 ? 1 : 0) : (selected >= 0 && selected < optionsHeld ? selected : -1);
	def->chips.push_back(std::move(chip));
	RefreshMenu(menu);
	return static_cast<int>(def->chips.size()) - 1;
}

int MenuManager::AddMenuNote(MenuHandle menu, const char *label, const char *value)
{
	ScopedLock lock(m_mutex);
	MenuDef *def = Find(menu);
	if (!def || static_cast<int>(def->chips.size()) >= panorama_hud::kChipSlots)
	{
		return -1;
	}
	MenuDef::Chip chip;
	chip.label = label ? label : "";
	chip.value = value ? value : "";
	chip.note = true;
	chip.selected = 0;
	def->chips.push_back(std::move(chip));
	RefreshMenu(menu);
	return static_cast<int>(def->chips.size()) - 1;
}

void MenuManager::SetMenuChipOptionTone(MenuHandle menu, int chip, int option, MenuTone tone)
{
	ScopedLock lock(m_mutex);
	MenuDef *def = Find(menu);
	if (!def || chip < 0 || chip >= static_cast<int>(def->chips.size()) || option < 0 || option >= static_cast<int>(def->chips[chip].options.size()))
	{
		return;
	}
	std::vector<MenuTone> &tones = def->chips[chip].tones;
	if (static_cast<int>(tones.size()) <= option)
	{
		tones.resize(option + 1, MenuTone::Info);
	}
	tones[option] = tone;
	RefreshMenu(menu);
}

void MenuManager::SetMenuChipCallback(MenuHandle menu, MenuChipFn onChip)
{
	ScopedLock lock(m_mutex);
	if (MenuDef *def = Find(menu))
	{
		def->onChip = std::move(onChip);
	}
}

// Stores a filter's new selection, then tells the owner, who usually rebuilds the menu. An action only tells.
void MenuManager::PickChip(int slot, int chip, int selected)
{
	PlayerMenu &pm = m_players[slot];
	const MenuHandle handle = pm.handle;
	MenuDef *def = Find(handle);
	if (!def || chip < 0 || chip >= static_cast<int>(def->chips.size()))
	{
		return;
	}
	if (!def->chips[chip].action)
	{
		def->chips[chip].selected = selected;
	}
	MenuChipFn onChip = def->onChip;
	{
		DepthGuard guard(m_callbackDepth);
		if (onChip && guard.enter())
		{
			onChip(handle, slot, chip, selected);
		}
	}
	if (pm.active && pm.handle == handle)
	{
		RenderPanorama(slot);
	}
}

int MenuManager::Segments(const MenuItem &item)
{
	const int options = static_cast<int>(item.options.size());
	return item.type == MenuItemType::Choice && options >= 2 && options <= panorama_hud::kSegments ? options : 0;
}

std::string MenuManager::OptionLabel(const std::string &option)
{
	return option.substr(0, option.find('\n'));
}

std::string MenuManager::OptionSub(const std::string &option)
{
	const size_t at = option.find('\n');
	return at == std::string::npos ? std::string() : option.substr(at + 1);
}

std::vector<MenuManager::Page> MenuManager::Pages(const MenuDef &def, MenuType type) const
{
	// m_itemsPerPage is clamped >= 1 in Configure.
	int size = m_itemsPerPage;
	const panorama_hud::Layout layout = type == MenuType::Panorama ? PanoramaLayout(def) : panorama_hud::Layout::List;
	// Showcase and studio pages are three columns of buttons.
	const bool buttons = type == MenuType::Panorama && (layout == panorama_hud::Layout::Showcase || layout == panorama_hud::Layout::Studio);
	const bool columns = type == MenuType::Panorama && layout == panorama_hud::Layout::Columns;
	if (type == MenuType::Chat)
	{
		// Less the pinned rows.
		size = (std::max)(1, size - (PinnedItem(def, type) >= 0 ? 1 : 0) - (SecondaryItem(def, type) >= 0 ? 1 : 0));
	}
	if (type == MenuType::Panorama)
	{
		size = layout == panorama_hud::Layout::Grid ? panorama_hud::TileSlots(TileSize(def))
			   : buttons                            ? panorama_hud::kShowcasePage
													: panorama_hud::ItemSlots(layout);
	}
	std::vector<Page> pages;
	const int count = static_cast<int>(def.items.size());
	int rows = 0;  // on the last page, those off the pages not counted
	int slots = 0; // the buttons they take there, a Choice drawn in place one per option
	for (int i = 0; i < count; i++)
	{
		if (OffPage(def, type, i))
		{
			continue;
		}
		const int section = def.items[i].section;
		// A tab with image tiles holds fewer than one of buttons.
		if (buttons && (pages.empty() || pages.back().section != section))
		{
			size = StudioImages(def, section) ? panorama_hud::kStudioImageSlots : panorama_hud::kShowcasePage;
		}
		const int cost = buttons ? (std::max)(1, Segments(def.items[i])) : 1;
		const bool full = rows >= size || (buttons && slots + cost > panorama_hud::kShowcaseSlots);
		if (pages.empty() || (!columns && (full || pages.back().section != section)))
		{
			pages.push_back({i, i, section});
			rows = 0;
			slots = 0;
		}
		pages.back().end = i + 1;
		rows++;
		slots += cost;
	}
	if (pages.empty())
	{
		pages.push_back({});
	}
	return pages;
}

std::vector<int> MenuManager::ChatPageItems(const MenuDef &def, const Page &page) const
{
	std::vector<int> shown;
	for (int i = page.first; i < page.end; i++)
	{
		if (!OffPage(def, MenuType::Chat, i))
		{
			shown.push_back(i);
		}
	}
	for (int item : {SecondaryItem(def, MenuType::Chat), PinnedItem(def, MenuType::Chat)})
	{
		if (item >= 0)
		{
			shown.push_back(item);
		}
	}
	return shown;
}

int MenuManager::PageOf(const MenuDef &def, MenuType type, int item) const
{
	const std::vector<Page> pages = Pages(def, type);
	for (int page = 0; page < static_cast<int>(pages.size()); page++)
	{
		if (item < pages[page].end)
		{
			return page;
		}
	}
	return static_cast<int>(pages.size()) - 1;
}

panorama_hud::Layout MenuManager::PanoramaLayout(const MenuDef &def) const
{
	const panorama_hud::Layout wanted = def.layout == MenuLayout::Grid       ? panorama_hud::Layout::Grid
										: def.layout == MenuLayout::Showcase ? panorama_hud::Layout::Showcase
										: def.layout == MenuLayout::Studio   ? panorama_hud::Layout::Studio
										: def.layout == MenuLayout::Columns  ? panorama_hud::Layout::Columns
																			 : panorama_hud::Layout::List;
	return panorama_hud::Available(wanted) ? wanted : panorama_hud::Layout::List;
}

std::string MenuManager::SuffixText(int slot, const MenuDef &def, const MenuItem &item) const
{
	if (item.role == MenuItemRole::Readout || item.role == MenuItemRole::Heading)
	{
		return std::string();
	}
	return item.type != MenuItemType::Normal ? ValueText(slot, def, item) : item.subtext;
}

std::string MenuManager::LineText(const MenuItem &item)
{
	return item.role == MenuItemRole::Readout && !item.subtext.empty() ? item.subtext + ": " + item.text : item.text;
}

bool MenuManager::Inert(const MenuItem &item)
{
	return item.disabled || item.role == MenuItemRole::Readout || item.role == MenuItemRole::Heading;
}

int MenuManager::ClampValue(const MenuItem &item, int value)
{
	switch (item.type)
	{
		case MenuItemType::Toggle:
			return value != 0 ? 1 : 0;
		case MenuItemType::Stepper:
			return (std::max)(item.min, (std::min)(value, item.max));
		case MenuItemType::Choice:
			return item.options.empty() ? 0 : (std::max)(0, (std::min)(value, static_cast<int>(item.options.size()) - 1));
		default:
			return value;
	}
}

std::string MenuManager::ValueText(int slot, const MenuDef &def, const MenuItem &item) const
{
	switch (item.type)
	{
		case MenuItemType::Toggle:
			return ResolveLabel(slot, def, item.value ? MenuLabel::On : MenuLabel::Off);
		case MenuItemType::Stepper:
			return std::to_string(item.value);
		case MenuItemType::Choice:
			return item.value < static_cast<int>(item.options.size()) ? OptionLabel(item.options[item.value]) : std::string();
		default:
			return std::string();
	}
}

void MenuManager::ChangeValue(int slot, int itemIndex, int value)
{
	const MenuHandle handle = m_players[slot].handle;
	MenuItem *item = FindItem(handle, itemIndex);
	if (!item)
	{
		return;
	}
	value = ClampValue(*item, value);
	if (value == item->value)
	{
		Render(slot); // chat reprints, like picking a disabled item
		return;
	}
	item->value = value;
	MenuItemChangeFn onChange = Find(handle)->onChange;
	{
		DepthGuard guard(m_callbackDepth);
		if (onChange && guard.enter())
		{
			onChange(handle, slot, itemIndex, value);
		}
	}
	RefreshMenu(handle);
}

void MenuManager::StepValue(int slot, int itemIndex, int steps)
{
	const MenuItem *item = FindItem(m_players[slot].handle, itemIndex);
	if (!item || item->disabled)
	{
		return;
	}
	int value = item->value;
	switch (item->type)
	{
		case MenuItemType::Toggle:
			value = value ? 0 : 1;
			break;
		case MenuItemType::Stepper:
		{
			// Widened so a big step near INT_MAX clamps instead of wrapping.
			const long long moved = static_cast<long long>(value) + static_cast<long long>(steps) * item->step;
			value = static_cast<int>((std::max)(static_cast<long long>(item->min), (std::min)(moved, static_cast<long long>(item->max))));
			break;
		}
		case MenuItemType::Choice:
		{
			const int count = static_cast<int>(item->options.size());
			if (count == 0)
			{
				return;
			}
			value = ((value + steps) % count + count) % count;
			break;
		}
		default:
			return;
	}
	ChangeValue(slot, itemIndex, value);
}

std::array<int, panorama_hud::kStepButtons> MenuManager::StepCounts(const MenuItem &item)
{
	const long long range = static_cast<long long>(item.max) - item.min;
	if (range >= 10LL * item.step)
	{
		return {-5, -1, 1, 5};
	}
	return {0, -1, 1, 0};
}

void MenuManager::ActivateValueItem(int slot, int itemIndex)
{
	PlayerMenu &pm = m_players[slot];
	const MenuItem *item = FindItem(pm.handle, itemIndex);
	if (!item)
	{
		return;
	}
	if (item->type == MenuItemType::Toggle)
	{
		StepValue(slot, itemIndex, 1);
		return;
	}
	pm.editItem = itemIndex;
	pm.editPage = 0;
	if (item->type == MenuItemType::Choice)
	{
		// Open on the page holding the current option.
		pm.editPage = item->value / (pm.type == MenuType::Panorama ? panorama_hud::kListSlots : m_itemsPerPage);
	}
	Render(slot);
}

const MenuManager::MenuItem *MenuManager::EditedItem(int slot) const
{
	const PlayerMenu &pm = m_players[slot];
	if (pm.editItem < 0)
	{
		return nullptr;
	}
	const MenuItem *item = FindItem(pm.handle, pm.editItem);
	if (!item || item->disabled || (item->type != MenuItemType::Stepper && item->type != MenuItemType::Choice))
	{
		return nullptr;
	}
	return item;
}

void MenuManager::StopEdit(int slot)
{
	m_players[slot].editItem = -1;
	m_players[slot].editPage = 0;
	Render(slot);
}

MenuType MenuManager::GetMenuType(MenuHandle menu) const
{
	ScopedLock lock(m_mutex);
	const MenuDef *def = Find(menu);
	return def ? def->type : MenuType::Default;
}

bool MenuManager::GetExitButton(MenuHandle menu) const
{
	ScopedLock lock(m_mutex);
	const MenuDef *def = Find(menu);
	return def ? def->exitButton : false;
}

bool MenuManager::GetCloseOnSelect(MenuHandle menu) const
{
	ScopedLock lock(m_mutex);
	const MenuDef *def = Find(menu);
	return def ? def->closeOnSelect : false;
}

bool MenuManager::GetExitItem(MenuHandle menu) const
{
	ScopedLock lock(m_mutex);
	const MenuDef *def = Find(menu);
	return def ? def->exitItem : false;
}

bool MenuManager::GetMenuForceType(MenuHandle menu) const
{
	ScopedLock lock(m_mutex);
	const MenuDef *def = Find(menu);
	return def ? def->forced : false;
}

MenuButton MenuManager::GetMenuKey(MenuHandle menu, MenuNavAction action) const
{
	ScopedLock lock(m_mutex);
	const MenuDef *def = Find(menu);
	int idx = static_cast<int>(action);
	if (!def || idx < 0 || idx > static_cast<int>(MenuNavAction::Back))
	{
		return MenuButton::Default;
	}
	uint64_t mask = def->navOverride[idx].mask;
	if (mask == 0)
	{
		return MenuButton::Default; // unset: inherits the server binding
	}
	if (mask == kNavDisabledSentinel)
	{
		return MenuButton::None; // disabled for this menu
	}
	const keys::KeyDef *k = keys::FindByMask(mask);
	return k ? k->button : MenuButton::Default;
}

bool MenuManager::HtmlShowsExitRow(const MenuDef &def, int slot) const
{
	// Must be exitable, and either explicitly requested or forced because the Back
	// key is disabled (otherwise the player would have no way out but the timeout).
	return def.exitButton && (def.exitItem || EffectiveNavMask(def, slot, MenuNavAction::Back) == 0);
}

int MenuManager::HtmlRowCount(const MenuDef &def, int slot) const
{
	const PlayerMenu &pm = m_players[slot];
	const size_t rows = pm.index ? IndexRanges(def, pm.type).size() : def.items.size();
	return static_cast<int>(rows) + (HtmlShowsExitRow(def, slot) ? 1 : 0);
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
	if (ValidSlot(slot))
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
	if (ValidSlot(slot))
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
	if (!def.forced && ValidSlot(slot) && m_prefs[slot].type != MenuType::Default)
	{
		result = m_prefs[slot].type;
	}
	// A style that can't render or take input falls back: panorama to HTML, HTML to chat.
	if (result == MenuType::Panorama && !panorama_hud::Available())
	{
		result = MenuType::Html;
	}
	if (result != MenuType::Chat && result != MenuType::Panorama)
	{
		result = m_htmlAvailable ? MenuType::Html : MenuType::Chat;
	}
	return result;
}

MenuType MenuManager::GetSlotMenuType(int slot, MenuType type) const
{
	ScopedLock lock(m_mutex);
	MenuDef def;
	def.type = type;
	return ResolveType(def, slot);
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
		case MenuLabel::On:
			return "On";
		case MenuLabel::Off:
			return "Off";
		case MenuLabel::Adjust:
			return "Adjust";
		case MenuLabel::Done:
			return "Done";
		case MenuLabel::Exit:
		default:
			return "Exit";
	}
}

std::string MenuManager::Lang(int slot) const
{
	return m_langResolver ? m_langResolver(slot) : std::string();
}

std::string MenuManager::ResolveLabel(int slot, const MenuDef &def, MenuLabel label) const
{
	return g_Translations.Translate(Lang(slot), def.labels[static_cast<int>(label)]);
}

bool MenuManager::DisplayMenu(MenuHandle menu, int slot, float duration, float curtime)
{
	ScopedLock lock(m_mutex);
	if (!ValidSlot(slot))
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

	// A CloseOnSelect pick that shows a fresh menu instead of pushing leaves its history behind.
	if (!m_players[slot].active && (!m_players[slot].back.empty() || !m_players[slot].forward.empty()))
	{
		EndMenus(slot, TakeHistory(slot), menu, MenuEndReason::Cancelled);
		def = Find(menu);
		if (!def || m_players[slot].externalBusy || m_players[slot].active)
		{
			return false;
		}
	}

	// Replace any existing menu first (fires its end callback).
	if (m_players[slot].active)
	{
		EndDisplay(slot, MenuEndReason::Cancelled);

		// That callback may have destroyed this menu (redisplaying a one-shot menu), marked the slot busy,
		// or opened a menu of its own, which is left alone rather than silently replaced.
		def = Find(menu);
		if (!def || m_players[slot].externalBusy || m_players[slot].active)
		{
			return false;
		}
	}

	PlayerMenu &pm = m_players[slot];
	pm.active = true;
	pm.suspended = false;
	pm.collapsed = false;
	pm.turning = false;
	pm.handle = menu;
	// Resolve the render type for this viewer (forced menu, else their preference, then HTML availability).
	// Fixed for the life of this display.
	pm.type = ResolveType(*def, slot);
	// Open on the configured start item (clamped at render time).
	pm.cursor = def->startItem;
	pm.page = PageOf(*def, pm.type, def->startItem);
	pm.expireTime = (duration > 0.0f) ? (m_curtime + duration) : 0.0f;
	pm.prevButtons = 0;
	pm.buttonsPrimed = false;
	pm.nextHtmlRender = 0.0f;
	pm.lastHtml.clear();
	pm.lastHtmlSend = 0.0f;
	pm.back.clear();
	pm.forward.clear();
	pm.selecting = HistoryEntry {};
	pm.editItem = -1;
	pm.editPage = 0;
	pm.index = false;
	EnterIndex(slot);

	Render(slot);
	return true;
}

bool MenuManager::PushMenu(MenuHandle menu, int slot, float duration, float curtime)
{
	ScopedLock lock(m_mutex);
	if (!ValidSlot(slot) || !Find(menu))
	{
		return false;
	}
	if (!OnMainThread())
	{
		m_pending.push_back(
			[this, menu, slot, duration]
			{
				if (Find(menu))
				{
					PushLocked(menu, slot, duration);
				}
			});
		return true;
	}
	m_curtime = curtime;
	return PushLocked(menu, slot, duration);
}

bool MenuManager::PushLocked(MenuHandle menu, int slot, float duration)
{
	PlayerMenu &pm = m_players[slot];
	const HistoryEntry from = pm.active ? Here(slot) : pm.selecting;
	// Nothing to go back to, or not allowed on this slot, is a plain display.
	if (from.handle == kInvalidMenuHandle || from.handle == menu || !Find(from.handle) || pm.externalBusy)
	{
		return DisplayLocked(menu, slot, duration);
	}

	DropForward(slot, menu);
	pm.back.push_back(from);
	pm.selecting = HistoryEntry {};
	// A CloseOnSelect pick already closed the display, the push reopens it. The timeout carries on.
	pm.active = true;
	SwitchMenu(slot, menu);
	return true;
}

bool MenuManager::ReplaceMenu(MenuHandle menu, int slot, float duration, float curtime)
{
	ScopedLock lock(m_mutex);
	if (!ValidSlot(slot) || !Find(menu))
	{
		return false;
	}
	if (!OnMainThread())
	{
		m_pending.push_back(
			[this, menu, slot, duration]
			{
				if (Find(menu))
				{
					ReplaceLocked(menu, slot, duration);
				}
			});
		return true;
	}
	m_curtime = curtime;
	return ReplaceLocked(menu, slot, duration);
}

bool MenuManager::ReplaceLocked(MenuHandle menu, int slot, float duration)
{
	PlayerMenu &pm = m_players[slot];
	if (!pm.active || pm.externalBusy)
	{
		return DisplayLocked(menu, slot, duration);
	}
	// Same place in the history, same page where it still exists.
	HistoryEntry here = Here(slot);
	const MenuHandle old = here.handle;
	here.handle = menu;
	here.cursor = Find(menu)->startItem;
	SwitchMenu(slot, menu, &here);
	EndMenus(slot, {old}, menu, MenuEndReason::Cancelled);
	return true;
}

void MenuManager::CancelMenu(int slot)
{
	ScopedLock lock(m_mutex);
	if (!ValidSlot(slot))
	{
		return;
	}
	// HTML clear + end-callback are main-thread only.
	// Off-thread defers to GameFrame.
	if (!OnMainThread())
	{
		// Only the menu open now, not one displayed between queueing and the drain.
		MenuHandle handle = m_players[slot].active ? m_players[slot].handle : kInvalidMenuHandle;
		m_pending.push_back(
			[this, slot, handle]
			{
				if (m_players[slot].active && m_players[slot].handle == handle)
				{
					EndDisplay(slot, MenuEndReason::Cancelled);
				}
			});
		return;
	}
	EndDisplay(slot, MenuEndReason::Cancelled);
}

bool MenuManager::BeginMenuInput(int slot, const char *prompt, const char *hint, MenuInputCancelFn onCancel)
{
	ScopedLock lock(m_mutex);
	if (!ValidSlot(slot) || !OnMainThread())
	{
		return false;
	}
	PlayerMenu &pm = m_players[slot];
	const MenuDef *def = Find(pm.handle);
	if (!pm.active || pm.suspended || pm.type != MenuType::Panorama || !def || PanoramaLayout(*def) == panorama_hud::Layout::List)
	{
		return false;
	}
	pm.editItem = -1;
	pm.inputMenu = pm.handle;
	pm.inputPrompt = prompt ? prompt : "";
	pm.inputHint = hint ? hint : "";
	pm.onInputCancel = std::move(onCancel);
	RenderPanorama(slot);
	return true;
}

void MenuManager::EndMenuInput(int slot)
{
	ScopedLock lock(m_mutex);
	if (!ValidSlot(slot))
	{
		return;
	}
	if (!OnMainThread())
	{
		m_pending.push_back([this, slot] { EndMenuInput(slot); });
		return;
	}
	PlayerMenu &pm = m_players[slot];
	if (pm.inputMenu == kInvalidMenuHandle)
	{
		return;
	}
	pm.inputMenu = kInvalidMenuHandle;
	pm.onInputCancel = nullptr;
	RedrawPanorama(slot);
}

bool MenuManager::ShowMenuMessage(int slot, const char *text, MenuTone tone, float seconds)
{
	ScopedLock lock(m_mutex);
	if (!ValidSlot(slot) || !OnMainThread())
	{
		return false;
	}
	PlayerMenu &pm = m_players[slot];
	if (!pm.active || pm.suspended || pm.type != MenuType::Panorama || !Find(pm.handle))
	{
		return false;
	}
	pm.message = panorama_hud::StripColors(text ? text : "");
	pm.messageTone = tone;
	pm.messageUntil = m_curtime + (std::max)(seconds, 0.5f);
	RenderPanorama(slot);
	return true;
}

bool MenuManager::ShowNotice(int slot, const char *title, const char *text, const char *hint, float seconds, MenuNoticeFn onClick)
{
	ScopedLock lock(m_mutex);
	if (!ValidSlot(slot) || !OnMainThread())
	{
		return false;
	}
	Notice &notice = m_notices[slot];
	notice = {true,
			  panorama_hud::StripColors(title ? title : ""),
			  panorama_hud::StripColors(text ? text : ""),
			  panorama_hud::StripColors(hint ? hint : ""),
			  seconds > 0.0f ? m_curtime + seconds : 0.0f,
			  std::move(onClick)};
	if (!RenderNotice(slot))
	{
		notice = {};
		return false;
	}
	return true;
}

void MenuManager::HideNotice(int slot)
{
	ScopedLock lock(m_mutex);
	if (!ValidSlot(slot) || !m_notices[slot].shown)
	{
		return;
	}
	if (!OnMainThread())
	{
		m_pending.push_back([this, slot] { HideNotice(slot); });
		return;
	}
	m_notices[slot] = {};
	panorama_hud::HideNotice(slot);
}

bool MenuManager::RenderNotice(int slot)
{
	Notice &notice = m_notices[slot];
	panorama_hud::Notice view {notice.title, std::string(), notice.text, notice.hint, m_settings.panoramaFontClass};
	notice.drawn = notice.until > 0.0f ? static_cast<int>(std::ceil(notice.until - m_curtime)) : -1;
	if (notice.drawn >= 0)
	{
		char time[16];
		snprintf(time, sizeof(time), "%d:%02d", notice.drawn / 60, notice.drawn % 60);
		view.time = time;
	}
	return panorama_hud::Available(panorama_hud::Layout::Notice) && panorama_hud::ShowNotice(slot, view);
}

bool MenuManager::ShowMenuConfirm(int slot, const char *title, const char *body, const char *cancel, const char *confirm, bool danger,
								  MenuConfirmFn onDone)
{
	ScopedLock lock(m_mutex);
	if (!ValidSlot(slot) || !OnMainThread())
	{
		return false;
	}
	PlayerMenu &pm = m_players[slot];
	const MenuDef *def = Find(pm.handle);
	if (!pm.active || pm.suspended || !def || (pm.type != MenuType::Panorama && !(def->textFeatures & kMenuTextConfirm)))
	{
		return false;
	}
	// A dialog already up gives way, answered as cancelled.
	MenuConfirmFn previous = std::move(pm.dialog.onDone);
	pm.editItem = -1;
	pm.dialog = {true,
				 panorama_hud::StripColors(title ? title : ""),
				 panorama_hud::StripColors(body ? body : ""),
				 panorama_hud::StripColors(cancel ? cancel : ""),
				 panorama_hud::StripColors(confirm ? confirm : ""),
				 danger,
				 std::move(onDone)};
	if (previous)
	{
		DepthGuard guard(m_callbackDepth);
		if (guard.enter())
		{
			previous(slot, false);
		}
	}
	Render(slot);
	return true;
}

void MenuManager::AnswerDialog(int slot, bool confirmed)
{
	PlayerMenu &pm = m_players[slot];
	const MenuHandle handle = pm.handle;
	MenuConfirmFn onDone = std::move(pm.dialog.onDone);
	pm.dialog = {};
	{
		DepthGuard guard(m_callbackDepth);
		if (onDone && guard.enter())
		{
			onDone(slot, confirmed);
		}
	}
	// Unless the handler showed something else.
	if (pm.active && pm.handle == handle && !pm.dialog.open)
	{
		Render(slot);
	}
}

void MenuManager::SetMenuEdited(MenuHandle menu, bool edited)
{
	ScopedLock lock(m_mutex);
	MenuDef *def = Find(menu);
	if (def && def->edited != edited)
	{
		def->edited = edited;
		RefreshMenu(menu);
	}
}

void MenuManager::SetMenuScope(MenuHandle menu, const char *label, int teams)
{
	ScopedLock lock(m_mutex);
	if (MenuDef *def = Find(menu))
	{
		def->scope = label ? label : "";
		def->scopeTeams = teams & (kMenuTeamT | kMenuTeamCT);
		RefreshMenu(menu);
	}
}

void MenuManager::SetMenuScopeCallback(MenuHandle menu, MenuScopeFn onScope)
{
	ScopedLock lock(m_mutex);
	if (MenuDef *def = Find(menu))
	{
		def->onScope = std::move(onScope);
		RefreshMenu(menu);
	}
}

void MenuManager::SetMenuInputClearCallback(MenuHandle menu, MenuInputClearFn onClear)
{
	ScopedLock lock(m_mutex);
	if (MenuDef *def = Find(menu))
	{
		def->onInputClear = std::move(onClear);
		RefreshMenu(menu);
	}
}

int MenuManager::AddMenuTab(MenuHandle menu, const char *label, bool selected, bool marked, bool pinned)
{
	ScopedLock lock(m_mutex);
	MenuDef *def = Find(menu);
	if (!def || static_cast<int>(def->tabs.size()) >= panorama_hud::kGridTabs)
	{
		return -1;
	}
	def->tabs.push_back({label ? label : "", selected, marked, pinned});
	RefreshMenu(menu);
	return static_cast<int>(def->tabs.size()) - 1;
}

void MenuManager::SetMenuTabCallback(MenuHandle menu, MenuTabFn onTab)
{
	ScopedLock lock(m_mutex);
	if (MenuDef *def = Find(menu))
	{
		def->onTab = std::move(onTab);
	}
}

void MenuManager::SetMenuEmpty(MenuHandle menu, const char *title, const char *text, bool loading)
{
	ScopedLock lock(m_mutex);
	if (MenuDef *def = Find(menu))
	{
		def->emptyTitle = title ? title : "";
		def->emptyText = text ? text : "";
		def->emptyLoading = loading;
		RefreshMenu(menu);
	}
}

bool MenuManager::AddMenuHint(int slot, const char *keys, const char *text)
{
	ScopedLock lock(m_mutex);
	if (!ValidSlot(slot) || !OnMainThread())
	{
		return false;
	}
	PlayerMenu &pm = m_players[slot];
	if (!pm.active || static_cast<int>(pm.hint.size()) >= panorama_hud::kHintParts)
	{
		return false;
	}
	pm.hint.emplace_back(keys ? keys : "", text ? text : "");
	pm.hintHidden = false;
	RedrawPanorama(slot);
	return true;
}

void MenuManager::ClearMenuHint(int slot)
{
	ScopedLock lock(m_mutex);
	if (!ValidSlot(slot) || (m_players[slot].hint.empty() && !m_players[slot].hintHidden))
	{
		return;
	}
	if (!OnMainThread())
	{
		m_pending.push_back([this, slot] { ClearMenuHint(slot); });
		return;
	}
	PlayerMenu &pm = m_players[slot];
	pm.hint.clear();
	pm.hintHidden = false;
	RedrawPanorama(slot);
}

void MenuManager::HideMenuHint(int slot)
{
	ScopedLock lock(m_mutex);
	if (!ValidSlot(slot) || (m_players[slot].hintHidden && m_players[slot].hint.empty()))
	{
		return;
	}
	if (!OnMainThread())
	{
		m_pending.push_back([this, slot] { HideMenuHint(slot); });
		return;
	}
	PlayerMenu &pm = m_players[slot];
	if (!pm.active)
	{
		return;
	}
	pm.hint.clear();
	pm.hintHidden = true;
	RedrawPanorama(slot);
}

bool MenuManager::AddMenuHelp(int slot, const char *keys, const char *text)
{
	ScopedLock lock(m_mutex);
	if (!ValidSlot(slot) || !OnMainThread())
	{
		return false;
	}
	PlayerMenu &pm = m_players[slot];
	if (!pm.active || static_cast<int>(pm.help.size()) >= panorama_hud::kHelpRows)
	{
		return false;
	}
	pm.help.emplace_back(keys ? keys : "", text ? text : "");
	RedrawPanorama(slot);
	return true;
}

void MenuManager::ClearMenuHelp(int slot)
{
	ScopedLock lock(m_mutex);
	if (!ValidSlot(slot) || m_players[slot].help.empty())
	{
		return;
	}
	if (!OnMainThread())
	{
		m_pending.push_back([this, slot] { ClearMenuHelp(slot); });
		return;
	}
	PlayerMenu &pm = m_players[slot];
	pm.help.clear();
	RedrawPanorama(slot);
}

void MenuManager::SetMenuMirrored(int slot, bool mirrored)
{
	ScopedLock lock(m_mutex);
	if (!ValidSlot(slot) || m_players[slot].mirrored == mirrored)
	{
		return;
	}
	if (!OnMainThread())
	{
		m_pending.push_back([this, slot, mirrored] { SetMenuMirrored(slot, mirrored); });
		return;
	}
	PlayerMenu &pm = m_players[slot];
	if (!pm.active)
	{
		return;
	}
	pm.mirrored = mirrored;
	RedrawPanorama(slot);
}

void MenuManager::SetMenuTextFeatures(MenuHandle menu, int features)
{
	ScopedLock lock(m_mutex);
	if (MenuDef *def = Find(menu))
	{
		def->textFeatures = features;
	}
}

int MenuManager::GetMenuTextFeatures(MenuHandle menu) const
{
	ScopedLock lock(m_mutex);
	const MenuDef *def = Find(menu);
	return def ? def->textFeatures : 0;
}

bool MenuManager::GetMenuEdited(MenuHandle menu) const
{
	ScopedLock lock(m_mutex);
	const MenuDef *def = Find(menu);
	return def && def->edited;
}

bool MenuManager::GuardLeave(int slot, bool wholeDisplay, std::function<void()> leave)
{
	PlayerMenu &pm = m_players[slot];
	const MenuDef *shown = Find(pm.handle);
	bool edited = shown && shown->edited;
	for (size_t i = 0; wholeDisplay && i < pm.back.size(); i++)
	{
		const MenuDef *def = Find(pm.back[i].handle);
		edited = edited || (def && def->edited);
	}
	if (!edited)
	{
		return false;
	}
	const std::string lang = Lang(slot);
	const std::string title = panorama_hud::StripColors(shown ? shown->title : std::string());
	const std::string body = FillTemplate(g_Translations.Translate(lang, "{title} has changes that aren't applied."), {{"title", title}});
	const MenuHandle handle = pm.handle;
	return ShowMenuConfirm(slot, g_Translations.Translate(lang, "Discard your edits?").c_str(), body.c_str(),
						   g_Translations.Translate(lang, "Keep editing").c_str(), g_Translations.Translate(lang, "Discard").c_str(), true,
						   [this, handle, leave](int s, bool confirmed)
						   {
							   if (confirmed && m_players[s].active && m_players[s].handle == handle)
							   {
								   leave();
							   }
						   });
}

void MenuManager::SuspendMenu(int slot)
{
	ScopedLock lock(m_mutex);
	if (!ValidSlot(slot))
	{
		return;
	}
	if (!OnMainThread())
	{
		m_pending.push_back([this, slot] { SuspendMenu(slot); });
		return;
	}
	PlayerMenu &pm = m_players[slot];
	if (!pm.active || pm.suspended)
	{
		return;
	}
	pm.suspended = true;
	// Tick hides a panorama window, chat pages just stop coming.
	if (pm.type == MenuType::Html)
	{
		center_html::Send(slot, kHtmlClearContent, kHtmlClearDurationSecs);
	}
}

void MenuManager::ResumeMenu(int slot)
{
	ScopedLock lock(m_mutex);
	if (!ValidSlot(slot))
	{
		return;
	}
	if (!OnMainThread())
	{
		m_pending.push_back([this, slot] { ResumeMenu(slot); });
		return;
	}
	PlayerMenu &pm = m_players[slot];
	if (!pm.active || !pm.suspended)
	{
		return;
	}
	pm.suspended = false;
	pm.lastHtml.clear();
	Render(slot);
}

bool MenuManager::HasMenu(int slot) const
{
	ScopedLock lock(m_mutex);
	if (!ValidSlot(slot))
	{
		return false;
	}
	return m_players[slot].active;
}

MenuHandle MenuManager::GetActiveMenu(int slot) const
{
	ScopedLock lock(m_mutex);
	if (!ValidSlot(slot) || !m_players[slot].active)
	{
		return kInvalidMenuHandle;
	}
	return m_players[slot].handle;
}

MenuType MenuManager::GetActiveMenuType(int slot) const
{
	ScopedLock lock(m_mutex);
	if (!ValidSlot(slot) || !m_players[slot].active)
	{
		return MenuType::Chat;
	}
	// Report the resolved per-viewer type so callers see chat vs html as actually rendered.
	return m_players[slot].type;
}

int MenuManager::GetSelectedItem(int slot) const
{
	ScopedLock lock(m_mutex);
	if (!ValidSlot(slot) || !m_players[slot].active)
	{
		return -1;
	}
	const PlayerMenu &pm = m_players[slot];
	const MenuDef *def = Find(pm.handle);
	// HTML only, and only when the cursor sits on a real item (not the Exit row).
	if (!def || pm.type != MenuType::Html || pm.index || pm.cursor < 0 || pm.cursor >= static_cast<int>(def->items.size()))
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
		// Use the per-viewer resolved type, not the menu's base type:
		// a Default/HTML menu can render as chat for some viewers, and only an HTML display needs the panel cleared.
		bool slotHtml = (pm.type == MenuType::Html);
		pm.active = false;
		pm.handle = kInvalidMenuHandle;

		if (onMain)
		{
			if (slotHtml)
			{
				center_html::Send(slot, kHtmlClearContent, kHtmlClearDurationSecs); // clear the panel immediately
			}
			DepthGuard guard(m_callbackDepth);
			if (onEnd && guard.enter())
			{
				onEnd(menu, slot, MenuEndReason::Destroyed);
			}
		}
		else if (slotHtml)
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

const char *MenuManager::GetItemText(MenuHandle menu, int item) const
{
	ScopedLock lock(m_mutex);
	const MenuItem *it = FindItem(menu, item);
	return it ? it->text.c_str() : "";
}

const char *MenuManager::GetItemInfo(MenuHandle menu, int item) const
{
	ScopedLock lock(m_mutex);
	const MenuItem *it = FindItem(menu, item);
	return it ? it->info.c_str() : "";
}

void MenuManager::EndDisplay(int slot, MenuEndReason reason)
{
	PlayerMenu &pm = m_players[slot];
	if (!pm.active)
	{
		return;
	}
	// An open dialog goes with it, unanswered, and so do the typing and the message line.
	pm.dialog = {};
	pm.hint.clear();
	pm.hintHidden = false;
	pm.help.clear();
	pm.helpOpen = false;
	pm.mirrored = false;
	pm.inputMenu = kInvalidMenuHandle;
	pm.onInputCancel = nullptr;
	pm.message.clear();
	pm.messageUntil = 0.0f;

	MenuHandle handle = pm.handle;
	pm.active = false;
	pm.suspended = false;
	pm.handle = kInvalidMenuHandle;

	// Every menu the display went through ends with it, not only the one on screen, so their owners can free them.
	std::vector<MenuHandle> menus = TakeHistory(slot);
	menus.push_back(handle);

	// Clear any HTML panel so it doesn't linger for its remaining duration.
	if (Find(handle) && pm.type == MenuType::Html)
	{
		center_html::Send(slot, kHtmlClearContent, kHtmlClearDurationSecs);
	}

	EndMenus(slot, menus, kInvalidMenuHandle, reason);
}

void MenuManager::EndMenus(int slot, std::vector<MenuHandle> menus, MenuHandle current, MenuEndReason reason)
{
	std::vector<MenuHandle> ended;
	for (MenuHandle menu : menus)
	{
		// Once each, and never one the player can still reach.
		if (menu == current || std::find(ended.begin(), ended.end(), menu) != ended.end() || InHistory(slot, menu))
		{
			continue;
		}
		ended.push_back(menu);
		// Copy the callback before invoking: the handler may DisplayMenu/DestroyMenu.
		if (MenuDef *def = Find(menu))
		{
			MenuEndFn onEnd = def->onEnd;
			DepthGuard guard(m_callbackDepth);
			if (onEnd && guard.enter())
			{
				onEnd(menu, slot, reason);
			}
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

	if (Inert(def->items[itemIndex]))
	{
		Render(slot); // re-render so the player can pick again
		return;
	}

	// Picking any row closes an open panorama popup, unless it opens its own.
	pm.editItem = -1;
	if (def->items[itemIndex].type != MenuItemType::Normal)
	{
		ActivateValueItem(slot, itemIndex);
		return;
	}

	// A submenu item navigates into its child instead of firing onSelect.
	MenuHandle sub = def->items[itemIndex].submenu;
	if (sub != kInvalidMenuHandle && Find(sub))
	{
		DropForward(slot, sub);
		pm.back.push_back(Here(slot));
		SwitchMenu(slot, sub);
		return;
	}

	MenuHandle handle = pm.handle;
	MenuItemSelectFn onSelect = def->onSelect;
	bool closeOnSelect = def->closeOnSelect;
	bool wasHtml = (pm.type == MenuType::Html);

	if (closeOnSelect)
	{
		pm.selecting = Here(slot);
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

		pm.selecting = HistoryEntry {};

		// Skipped when onSelect showed this same menu again, or pushed it into the history of what it opened.
		bool reopened = pm.active && pm.handle == handle;
		if (!pm.active)
		{
			// The pick ended the display, and the history ends with it.
			std::vector<MenuHandle> menus = TakeHistory(slot);
			menus.push_back(handle);
			EndMenus(slot, menus, kInvalidMenuHandle, MenuEndReason::Selected);
		}
		else if (!reopened)
		{
			EndMenus(slot, {handle}, pm.handle, MenuEndReason::Selected);
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

void MenuManager::SwitchMenu(int slot, MenuHandle handle, const HistoryEntry *restore)
{
	PlayerMenu &pm = m_players[slot];
	const MenuDef *newDef = Find(handle);
	if (!newDef)
	{
		return;
	}
	pm.suspended = false;

	// Re-resolve the render type for the menu we're switching to (a submenu may be forced to a different type than the parent).
	// Clear the HTML panel only when leaving HTML,
	// otherwise the re-render overwrites it.
	bool oldHtml = pm.type == MenuType::Html;
	pm.type = ResolveType(*newDef, slot);
	bool newHtml = pm.type == MenuType::Html;
	if (oldHtml && !newHtml)
	{
		center_html::Send(slot, kHtmlClearContent, kHtmlClearDurationSecs);
	}

	pm.handle = handle;
	pm.cursor = restore ? restore->cursor : newDef->startItem;
	pm.page = restore ? restore->page : PageOf(*newDef, pm.type, newDef->startItem);
	// Re-baseline buttons so the key that triggered the switch doesn't act again in the new menu.
	pm.prevButtons = 0;
	pm.buttonsPrimed = false;
	pm.nextHtmlRender = 0.0f;
	pm.lastHtml.clear();
	pm.editItem = -1;
	pm.editPage = 0;
	pm.index = restore && restore->index;
	if (!restore)
	{
		EnterIndex(slot);
	}
	// expireTime is kept so the whole submenu stack shares one timeout.

	Render(slot);
}

std::vector<MenuHandle> MenuManager::TakeHistory(int slot)
{
	PlayerMenu &pm = m_players[slot];
	std::vector<MenuHandle> menus;
	for (const std::vector<HistoryEntry> *stack : {&pm.back, &pm.forward})
	{
		for (const HistoryEntry &entry : *stack)
		{
			menus.push_back(entry.handle);
		}
	}
	pm.back.clear();
	pm.forward.clear();
	return menus;
}

MenuManager::HistoryEntry MenuManager::Here(int slot) const
{
	const PlayerMenu &pm = m_players[slot];
	return HistoryEntry {pm.handle, pm.page, pm.cursor, pm.index};
}

bool MenuManager::InHistory(int slot, MenuHandle menu) const
{
	const PlayerMenu &pm = m_players[slot];
	if (pm.active && pm.handle == menu)
	{
		return true;
	}
	for (const std::vector<HistoryEntry> *stack : {&pm.back, &pm.forward})
	{
		for (const HistoryEntry &entry : *stack)
		{
			if (entry.handle == menu)
			{
				return true;
			}
		}
	}
	return false;
}

bool MenuManager::StepBack(int slot, int steps)
{
	ScopedLock lock(m_mutex);
	if (!ValidSlot(slot) || !OnMainThread())
	{
		return false;
	}
	PlayerMenu &pm = m_players[slot];
	if (!pm.active || pm.suspended)
	{
		return false;
	}
	pm.editItem = -1;
	bool moved = false;
	for (int i = 0; i < steps && StepBack(slot); i++)
	{
		moved = true;
	}
	return moved;
}

bool MenuManager::StepBack(int slot)
{
	PlayerMenu &pm = m_players[slot];
	while (!pm.back.empty())
	{
		HistoryEntry entry = pm.back.back();
		pm.back.pop_back();
		if (Find(entry.handle))
		{
			pm.forward.push_back(Here(slot));
			SwitchMenu(slot, entry.handle, &entry);
			return true;
		}
	}
	return false;
}

bool MenuManager::StepForward(int slot)
{
	PlayerMenu &pm = m_players[slot];
	while (!pm.forward.empty())
	{
		HistoryEntry entry = pm.forward.back();
		pm.forward.pop_back();
		if (Find(entry.handle))
		{
			pm.back.push_back(Here(slot));
			SwitchMenu(slot, entry.handle, &entry);
			return true;
		}
	}
	return false;
}

bool MenuManager::HasBack(int slot) const
{
	for (const HistoryEntry &entry : m_players[slot].back)
	{
		if (Find(entry.handle))
		{
			return true;
		}
	}
	return false;
}

bool MenuManager::HasForward(int slot) const
{
	for (const HistoryEntry &entry : m_players[slot].forward)
	{
		if (Find(entry.handle))
		{
			return true;
		}
	}
	return false;
}

void MenuManager::DropForward(int slot, MenuHandle opening)
{
	PlayerMenu &pm = m_players[slot];
	std::vector<MenuHandle> menus;
	for (const HistoryEntry &entry : pm.forward)
	{
		menus.push_back(entry.handle);
	}
	pm.forward.clear();
	EndMenus(slot, menus, opening, MenuEndReason::Cancelled);
}

bool MenuManager::ProcessInput(int slot, const char *text, float curtime)
{
	ScopedLock lock(m_mutex);
	if (!ValidSlot(slot))
	{
		return false;
	}

	PlayerMenu &pm = m_players[slot];
	if (!pm.active || pm.suspended)
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
	// 1 confirms, 2 or 0 cancels.
	if (pm.dialog.open)
	{
		if (num >= 0 && num <= 2)
		{
			AnswerDialog(slot, num == 1);
		}
		return true;
	}
	if (const MenuItem *edited = EditedItem(slot))
	{
		ApplyEditNumber(slot, *edited, num);
		return true;
	}
	if (pm.index && num != 0)
	{
		OpenRange(slot, num - 1);
		return true;
	}

	const std::vector<Page> pages = Pages(*def, pm.type);
	int pageCount = static_cast<int>(pages.size());
	pm.page = (std::min)(pm.page, pageCount - 1);
	bool hasMore = (pm.page + 1 < pageCount);
	bool hasPrev = (pm.page > 0);

	const std::vector<int> shown = ChatPageItems(*def, pages[pm.page]);

	// Key layout: the rows from 1, Next = itemsPerPage+1, Prev = +2, Exit/Back = 0.
	if (num == 0)
	{
		if (EnterIndex(slot))
		{
			RenderPage(slot);
			return true;
		}
		// In a submenu, 0 steps back to the parent. Otherwise it exits.
		if (StepBack(slot))
		{
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

	if (num >= 1 && num <= static_cast<int>(shown.size()))
	{
		Select(slot, shown[num - 1]);
		return true;
	}

	// Numeric but out of range: consume so it doesn't leak into chat.
	return true;
}

bool MenuManager::WantsButtonInput(int slot) const
{
	ScopedLock lock(m_mutex);
	if (!ValidSlot(slot))
	{
		return false;
	}
	if (m_notices[slot].onClick)
	{
		return true;
	}
	if (!m_players[slot].active)
	{
		return false;
	}
	// Use the resolved per-viewer type, not the menu's base type. A turning studio waits for an attack press.
	return m_players[slot].type == MenuType::Html || m_players[slot].turning;
}

bool MenuManager::AnyHtmlMenuActive() const
{
	ScopedLock lock(m_mutex);
	for (int i = 0; i <= MAXPLAYERS; i++)
	{
		if (m_players[i].active && m_players[i].type == MenuType::Html)
		{
			return true;
		}
	}
	return false;
}

void MenuManager::PollButtons(int slot, uint64_t heldButtons, uint64_t pressedButtons, float curtime)
{
	ScopedLock lock(m_mutex);
	if (!ValidSlot(slot))
	{
		return;
	}
	// A click on the box itself only comes in cursor mode.
	if (m_notices[slot].onClick && (heldButtons & in_button::Score) && (pressedButtons & in_button::Attack))
	{
		OnPanoramaClick(slot, panorama_hud::Click::Notice, -1, curtime);
		return;
	}
	PlayerMenu &pm = m_players[slot];
	if (!pm.active || pm.suspended)
	{
		return;
	}
	MenuDef *def = Find(pm.handle);
	if (!def || (pm.type != MenuType::Html && !pm.turning))
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

	// Rising edge on the held mask, plus any button tapped and released inside this tick,
	// which never shows up in the held mask at all.
	// A button pressed and then kept down appears in both terms on the same tick, so it still only fires once.
	uint64_t newly = (heldButtons & ~pm.prevButtons) | pressedButtons;
	pm.prevButtons = heldButtons;
	if (newly == 0)
	{
		return;
	}

	// The studio's cursor comes back.
	if (pm.turning)
	{
		if (newly & in_button::Attack)
		{
			pm.turning = false;
			RenderPanorama(slot);
		}
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
	if (pm.dialog.open)
	{
		AnswerDialog(slot, pm.dialog.row == 0);
		return;
	}
	if (EditedItem(slot))
	{
		StopEdit(slot);
		return;
	}
	// Selecting the inline Exit row closes the menu, otherwise pick the cursor item.
	if (HtmlShowsExitRow(*def, slot) && pm.cursor == HtmlRowCount(*def, slot) - 1)
	{
		EndDisplay(slot, MenuEndReason::Exit);
	}
	else if (pm.index)
	{
		OpenRange(slot, pm.cursor);
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
	if (pm.dialog.open)
	{
		AnswerDialog(slot, false);
		return;
	}
	if (EditedItem(slot))
	{
		StopEdit(slot);
		return;
	}
	if (EnterIndex(slot))
	{
		Render(slot);
		return;
	}
	// Step up to the parent in a submenu, else exit (if exitable).
	if (!StepBack(slot) && def->exitButton)
	{
		EndDisplay(slot, MenuEndReason::Exit);
	}
}

void MenuManager::CommandNav(int slot, MenuNavAction action, float curtime)
{
	ScopedLock lock(m_mutex);
	if (!ValidSlot(slot))
	{
		return;
	}
	PlayerMenu &pm = m_players[slot];
	if (!pm.active || pm.suspended)
	{
		return;
	}
	MenuDef *def = Find(pm.handle);
	if (!def)
	{
		return;
	}
	m_curtime = curtime;

	// Up/Down/Select drive the HTML cursor (chat menus use typed numbers, panorama menus are clicked).
	// Back/close works for every render type.
	bool html = (pm.type == MenuType::Html);
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
	if (!ValidSlot(slot))
	{
		return;
	}
	PlayerMenu &pm = m_players[slot];
	if (!pm.active || pm.suspended)
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
	else if (pm.type == MenuType::Html)
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
	if (pm.dialog.open)
	{
		pm.dialog.row = pm.dialog.row == 0 ? 1 : 0;
		RenderHtml(slot);
		return;
	}
	// Up raises the edited value.
	if (EditedItem(slot))
	{
		StepValue(slot, pm.editItem, -delta);
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
		if (pm.messageUntil > 0.0f && curtime >= pm.messageUntil)
		{
			pm.messageUntil = 0.0f;
			pm.message.clear();
			RedrawPanorama(i);
		}
		// HTML messages decay, refresh periodically.
		const MenuDef *def = Find(pm.handle);
		if (def && !pm.suspended && pm.type == MenuType::Html && curtime >= pm.nextHtmlRender)
		{
			RenderHtml(i);
		}
	}

	// Panorama windows don't decay, they only need hiding once their display ends.
	// Hiding here, not in EndDisplay, keeps the cursor up when one menu replaces another.
	for (int i = 0; i < MAXPLAYERS; i++)
	{
		const PlayerMenu &pm = m_players[i];
		if (!pm.active || pm.suspended || pm.type != MenuType::Panorama)
		{
			panorama_hud::Hide(i);
		}
		else if (!panorama_hud::IsShown(i))
		{
			Render(i); // the window went away, e.g. with the map
		}
	}

	for (int i = 0; i < MAXPLAYERS; i++)
	{
		const Notice &notice = m_notices[i];
		if (!notice.shown)
		{
			continue;
		}
		if (notice.until > 0.0f && curtime >= notice.until)
		{
			HideNotice(i);
		}
		else if (notice.until > 0.0f && static_cast<int>(std::ceil(notice.until - curtime)) != notice.drawn)
		{
			RenderNotice(i);
		}
	}
}

void MenuManager::OnPlayerDisconnect(int slot)
{
	ScopedLock lock(m_mutex);
	if (!ValidSlot(slot))
	{
		return;
	}
	EndDisplay(slot, MenuEndReason::Disconnect);
	// Drop busy state so a reconnecting client on this slot starts clean.
	m_players[slot].externalBusy = false;
	m_notices[slot] = {};
}

void MenuManager::SetExternalBusy(int slot, bool busy)
{
	ScopedLock lock(m_mutex);
	if (!ValidSlot(slot))
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
		MenuHandle handle = m_players[slot].handle;
		m_pending.push_back(
			[this, slot, handle]
			{
				if (m_players[slot].active && m_players[slot].handle == handle)
				{
					EndDisplay(slot, MenuEndReason::Cancelled);
				}
			});
		return;
	}
	EndDisplay(slot, MenuEndReason::Cancelled);
}

bool MenuManager::GetExternalBusy(int slot) const
{
	ScopedLock lock(m_mutex);
	if (!ValidSlot(slot))
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
	if (!ValidSlot(slot))
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
	if (!ValidSlot(slot))
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
	if (!ValidSlot(slot))
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
	if (!ValidSlot(slot))
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
	if (!ValidSlot(slot))
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
	// Changes to a suspended menu show once it resumes.
	if (!def || m_players[slot].suspended)
	{
		return;
	}
	switch (m_players[slot].type)
	{
		case MenuType::Chat:
			RenderPage(slot);
			break;
		case MenuType::Panorama:
			RenderPanorama(slot);
			break;
		default:
			RenderHtml(slot);
			break;
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

	if (pm.dialog.open)
	{
		std::vector<ChatRow> rows;
		if (!pm.dialog.body.empty())
		{
			rows.push_back({pm.dialog.body, false, false, true});
		}
		rows.push_back({pm.dialog.confirm});
		rows.push_back({pm.dialog.cancel});
		PrintChatPage(slot, *def, pm.dialog.title, std::string(), rows, 0, 1, false);
		return;
	}

	if (const MenuItem *edited = EditedItem(slot))
	{
		RenderEditPage(slot, *def, *edited);
		return;
	}

	const std::vector<IndexRange> ranges = IndexRanges(*def, pm.type);
	// A live item removal can leave the list too short for an index.
	pm.index = pm.index && !ranges.empty();
	if (pm.index)
	{
		std::vector<ChatRow> rows;
		for (const IndexRange &range : ranges)
		{
			rows.push_back({range.label});
		}
		PrintChatPage(slot, *def, def->title, std::string(), rows, 0, 1, def->exitButton);
		return;
	}

	const std::vector<Page> pages = Pages(*def, pm.type);
	const int pageCount = static_cast<int>(pages.size());
	// Clamp a page left stale by a live item removal / start-item past the end.
	if (pm.page >= pageCount)
	{
		pm.page = pageCount - 1;
	}
	const Page &page = pages[pm.page];

	std::vector<ChatRow> rows;
	for (int i : ChatPageItems(*def, page))
	{
		const MenuItem &item = def->items[i];
		ChatRow row;
		row.text = LineText(item);
		row.disabled = Inert(item);
		const std::string suffix = SuffixText(slot, *def, item);
		if (!suffix.empty())
		{
			// Disabled keeps the whole row grey.
			row.text += (item.disabled ? std::string() : m_settings.chatValueColor) + FillTemplate(m_settings.chatValueFormat, {{"value", suffix}});
		}
		rows.push_back(std::move(row));
	}
	const std::string section = page.section >= 0 ? def->sections[page.section] : std::string();
	// 0 goes back to the index even without an exit button.
	PrintChatPage(slot, *def, def->title, section, rows, pm.page, pageCount, def->exitButton || !ranges.empty());
}

void MenuManager::PrintChatPage(int slot, const MenuDef &def, const std::string &titleText, const std::string &section,
								const std::vector<ChatRow> &rows, int page, int pageCount, bool exitRow)
{
	const MenuManagerSettings &s = m_settings;

	// Optional branded header line above the title.
	if (!s.chatHeader.empty())
	{
		std::string header = s.chatHeaderColor + s.chatHeader;
		MENU_PrintToChat(slot, "%s", header.c_str());
	}

	// Built as std::string so long titles/items aren't truncated by a fixed buffer.
	// {title} holds the title text plus the optional page indicator
	// (in its own color, then the title color restored so the format's trailing decoration keeps the title color).
	std::string inner = titleText;
	if (pageCount > 1 && s.chatShowPage)
	{
		std::string indicator = FillTemplate(s.chatPageFormat, {{"cur", std::to_string(page + 1)}, {"total", std::to_string(pageCount)}});
		inner += " " + s.chatPageColor + indicator + s.chatTitleColor;
	}
	std::string title = s.chatTitleColor + FillTemplate(s.chatTitleFormat, {{"title", inner}});
	MENU_PrintToChat(slot, "%s", title.c_str());
	if (!section.empty())
	{
		std::string header = s.chatSectionColor + FillTemplate(s.chatSectionFormat, {{"section", section}});
		MENU_PrintToChat(slot, "%s", header.c_str());
	}

	// Item rows: color + numbered template + text. The number is the on-screen 1..N selection key.
	int number = 0;
	for (const ChatRow &row : rows)
	{
		if (row.note)
		{
			MENU_PrintToChat(slot, "%s%s", s.chatItemColor.c_str(), row.text.c_str());
			continue;
		}
		std::string num = std::to_string(++number);
		std::string line = row.disabled ? (s.chatDisabledColor + FillTemplate(s.chatDisabledFormat, {{"n", num}}))
										: (s.chatItemColor + FillTemplate(s.chatNumberFormat, {{"n", num}}));
		if (row.current && !row.disabled)
		{
			line += s.chatArrowColor;
		}
		line += row.text;
		MENU_PrintToChat(slot, "%s", line.c_str());
	}

	// Nav/exit rows: item-colored number, then arrow-colored arrow + label.
	auto navRow = [&](int key, MenuLabel label)
	{
		std::string line = s.chatItemColor + FillTemplate(s.chatNumberFormat, {{"n", std::to_string(key)}});
		line += s.chatArrowColor + s.chatArrow + ResolveLabel(slot, def, label);
		MENU_PrintToChat(slot, "%s", line.c_str());
	};
	if (page + 1 < pageCount)
	{
		navRow(m_itemsPerPage + 1, MenuLabel::NextPage);
	}
	if (page > 0)
	{
		navRow(m_itemsPerPage + 2, MenuLabel::PrevPage);
	}
	if (exitRow)
	{
		navRow(0, MenuLabel::Exit);
	}
}

void MenuManager::RenderEditPage(int slot, const MenuDef &def, const MenuItem &item)
{
	PlayerMenu &pm = m_players[slot];
	const std::string title = item.text + FillTemplate(m_settings.chatValueFormat, {{"value", ValueText(slot, def, item)}});
	std::vector<ChatRow> rows;
	if (item.type == MenuItemType::Stepper)
	{
		for (int count : StepCounts(item))
		{
			if (count == 0)
			{
				continue;
			}
			ChatRow row;
			row.text = (count > 0 ? "+" : "") + std::to_string(static_cast<long long>(count) * item.step);
			row.disabled = count < 0 ? item.value <= item.min : item.value >= item.max;
			rows.push_back(std::move(row));
		}
		PrintChatPage(slot, def, title, std::string(), rows, 0, 1, true);
		return;
	}

	const int optionCount = static_cast<int>(item.options.size());
	const int pageCount = (std::max)(1, (optionCount + m_itemsPerPage - 1) / m_itemsPerPage);
	pm.editPage = (std::max)(0, (std::min)(pm.editPage, pageCount - 1));
	const int first = pm.editPage * m_itemsPerPage;
	const int last = (std::min)(first + m_itemsPerPage, optionCount);
	for (int i = first; i < last; i++)
	{
		ChatRow row;
		row.text = OptionLabel(item.options[i]);
		row.current = i == item.value;
		rows.push_back(std::move(row));
	}
	PrintChatPage(slot, def, title, std::string(), rows, pm.editPage, pageCount, true);
}

void MenuManager::ApplyEditNumber(int slot, const MenuItem &item, int num)
{
	PlayerMenu &pm = m_players[slot];
	const int itemIndex = pm.editItem;
	// 0 leaves the edit even when the menu itself can't be exited.
	if (num == 0)
	{
		StopEdit(slot);
		return;
	}
	if (item.type == MenuItemType::Stepper)
	{
		int row = 0;
		for (int count : StepCounts(item))
		{
			if (count != 0 && ++row == num)
			{
				StepValue(slot, itemIndex, count);
				return;
			}
		}
		return;
	}

	const int optionCount = static_cast<int>(item.options.size());
	const int pageCount = (std::max)(1, (optionCount + m_itemsPerPage - 1) / m_itemsPerPage);
	if (num == m_itemsPerPage + 1 && pm.editPage + 1 < pageCount)
	{
		pm.editPage++;
		RenderPage(slot);
		return;
	}
	if (num == m_itemsPerPage + 2 && pm.editPage > 0)
	{
		pm.editPage--;
		RenderPage(slot);
		return;
	}
	const int option = pm.editPage * m_itemsPerPage + num - 1;
	if (num >= 1 && num <= m_itemsPerPage && option < optionCount)
	{
		// A pick closes the list, like a dropdown.
		pm.editItem = -1;
		pm.editPage = 0;
		ChangeValue(slot, itemIndex, option);
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
	const std::vector<IndexRange> ranges = IndexRanges(*def, pm.type);
	pm.index = pm.index && !ranges.empty();
	int itemCount = static_cast<int>(pm.index ? ranges.size() : items.size());
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
	// A string field falls back to the server default when its override is empty.
	auto pick = [](const std::string &override_, const std::string &dflt) -> const std::string & { return override_.empty() ? dflt : override_; };
	// A tri-state flag (-1 inherit / 0 off / 1 on) resolves to the server default when -1.
	auto pickFlag = [](int override_, bool dflt) { return override_ < 0 ? dflt : override_ != 0; };
	const std::string &titleColor = pick(st.titleColor, m_settings.titleColor);
	const std::string &navColor = pick(st.navColor, m_settings.navColor);
	const std::string &footerColor = pick(st.footerColor, m_settings.footerColor);
	const std::string &disabledColor = pick(st.disabledColor, m_settings.disabledColor);
	const std::string &itemColor = pick(st.itemColor, m_settings.itemColor);
	const std::string &fontFace = pick(st.fontFace, m_settings.fontFace);
	const std::string &marker = pick(st.marker, m_settings.marker);
	const std::string &counterColor = pick(st.counterColor, m_settings.counterColor);
	const std::string &submenuSuffix = pick(st.submenuSuffix, m_settings.submenuSuffix);
	const std::string &footerSep = pick(st.footerSeparator, m_settings.footerSeparator);
	const std::string &counterFormat = pick(st.counterFormat, m_settings.counterFormat);
	const std::string &footerHintFormat = pick(st.footerHintFormat, m_settings.footerHintFormat);
	const std::string &footerRangeFormat = pick(st.footerRangeFormat, m_settings.footerRangeFormat);
	const std::string &valueFormat = pick(st.valueFormat, m_settings.valueFormat);
	const std::string &editFormat = pick(st.editFormat, m_settings.editFormat);
	const std::string &sectionFormat = pick(st.sectionFormat, m_settings.sectionFormat);
	const std::string &sectionColor = pick(st.sectionColor, m_settings.sectionColor);
	const std::string &align = pick(st.align, m_settings.align);
	bool showCounter = pickFlag(st.showCounter, m_settings.showCounter);
	bool showFooter = pickFlag(st.showFooter, m_settings.showFooter);
	bool highlightText = pickFlag(st.highlightText, m_settings.highlightText);
	bool rawTitle = (st.rawTitle == 1); // per-menu only, no server default
	// Suffix appended to every class list: alignment, then an optional font face.
	std::string commonCls = AlignClass(align);
	if (!fontFace.empty())
	{
		commonCls += " " + fontFace;
	}
	const std::string titleCls = SizeClass(pick(st.titleSize, m_settings.titleSize)) + commonCls;
	const std::string itemCls = SizeClass(pick(st.itemSize, m_settings.itemSize)) + commonCls;
	const std::string footerCls = SizeClass(pick(st.footerSize, m_settings.footerSize)) + commonCls;
	const std::string counterCls = SizeClass(pick(st.counterSize, m_settings.counterSize)) + commonCls;
	const std::string markerHtml = center_html::Escape(marker);
	// While set, Up/Down adjust this row instead of moving the cursor.
	const MenuItem *edited = EditedItem(slot);

	std::string html;
	html.reserve(512);

	if (pm.dialog.open)
	{
		html += center_html::ColorizeChat(pm.dialog.title, titleColor.c_str(), titleCls.c_str());
		html += "<br>";
		if (!pm.dialog.body.empty())
		{
			html += center_html::ColorizeChat(pm.dialog.body, itemColor.c_str(), itemCls.c_str());
			html += "<br>";
		}
		const std::string *labels[] = {&pm.dialog.confirm, &pm.dialog.cancel};
		for (int row = 0; row < 2; row++)
		{
			const bool selected = row == pm.dialog.row;
			if (selected)
			{
				html += "<font color='" + navColor + "' class='" + itemCls + "'>" + markerHtml + "</font>";
			}
			html += center_html::ColorizeChat(*labels[row], (selected && highlightText) ? navColor.c_str() : itemColor.c_str(), itemCls.c_str());
			html += "<br>";
		}
		SendHtml(slot, html);
		return;
	}

	// Title + position counter (counter dimmed so the title reads first).
	// Raw title keeps the title font/size/color wrapper but emits the text verbatim (like SetItemRaw).
	if (rawTitle)
	{
		html += "<font color='" + titleColor + "' class='" + titleCls + "'>" + def->title + "</font>";
	}
	else
	{
		html += center_html::ColorizeChat(def->title, titleColor.c_str(), titleCls.c_str());
	}
	if (showCounter && count > 0)
	{
		std::string counter = FillTemplate(counterFormat, {{"cur", std::to_string(pm.cursor + 1)}, {"total", std::to_string(count)}});
		html += " <font class='" + counterCls + "' color='" + counterColor + "'>";
		html += center_html::Escape(counter);
		html += "</font>";
	}
	html += "<br>";

	// Scrolling window centered on the cursor. Per-menu override clamps to the same ceiling.
	int effVisible = (st.visibleItems > 0) ? (std::min)(st.visibleItems, MENU_MAX_HTML_VISIBLE) : m_htmlVisibleItems;
	int vis = (std::min)(effVisible, count);
	int start = pm.cursor - vis / 2;
	if (start < 0)
	{
		start = 0;
	}
	if (start + vis > count)
	{
		start = (std::max)(0, count - vis);
	}
	// Section headers are lines too: rows give way, from the end further off the cursor.
	auto headed = [&](int i) { return i < itemCount && items[i].section >= 0 && (i == start || items[i - 1].section != items[i].section); };
	auto lines = [&]
	{
		int total = vis;
		for (int i = start; i < start + vis; i++)
		{
			total += headed(i) ? 1 : 0;
		}
		return total;
	};
	while (vis > 1 && lines() > effVisible)
	{
		start += pm.cursor - start > start + vis - 1 - pm.cursor ? 1 : 0;
		vis--;
	}

	std::vector<size_t> rowStarts;
	for (int i = start; i < start + vis; i++)
	{
		rowStarts.push_back(html.size());
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

		if (pm.index)
		{
			if (selected)
			{
				html += "<font color='" + navColor + "' class='" + itemCls + "'>" + markerHtml + "</font>";
			}
			html += center_html::ColorizeChat(ranges[i].label, (selected && highlightText) ? navColor.c_str() : itemColor.c_str(), itemCls.c_str());
			html += "<br>";
			continue;
		}

		const MenuItem &item = items[i];

		// Above a section's first item, and atop a window scrolled into a section.
		if (item.section >= 0 && (i == start || items[i - 1].section != item.section))
		{
			html += "<font color='" + sectionColor + "' class='" + itemCls + "'>";
			html += center_html::Escape(FillTemplate(sectionFormat, {{"section", def->sections[item.section]}}));
			html += "</font><br>";
		}

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

		const char *base = Inert(item) ? disabledColor.c_str() : ((selected && highlightText) ? navColor.c_str() : itemColor.c_str());
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
			html += center_html::ColorizeChat(LineText(item), base, itemCls.c_str());
		}
		if (std::string value = SuffixText(slot, *def, item); !value.empty())
		{
			const bool editing = i == pm.editItem && edited;
			if (editing)
			{
				value = FillTemplate(editFormat, {{"value", value}});
			}
			html += "<font color='";
			html += editing ? navColor.c_str() : base;
			html += "' class='" + itemCls + "'>" + center_html::Escape(FillTemplate(valueFormat, {{"value", value}})) + "</font>";
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

	// The engine warns on every message over 1500 bytes, so rows give way until it fits with the footer.
	constexpr size_t kRowBudget = 1250;
	if (!rowStarts.empty())
	{
		rowStarts.push_back(html.size());
		int first = 0;
		int last = static_cast<int>(rowStarts.size()) - 1;
		const int cursorRow = pm.cursor - start;
		while (last - first > 1 && rowStarts[0] + rowStarts[last] - rowStarts[first] > kRowBudget)
		{
			if (cursorRow - first > last - 1 - cursorRow)
			{
				first++;
			}
			else
			{
				last--;
			}
		}
		html = html.substr(0, rowStarts[0]) + html.substr(rowStarts[first], rowStarts[last] - rowStarts[first]);
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

	// Build one footer hint via the format, then escape the whole (label + punctuation + keys).
	auto hint = [&](MenuLabel label, const std::string &keys)
	{
		std::string seg = FillTemplate(footerHintFormat, {{"label", ResolveLabel(slot, *def, label)}, {"keys", keys}});
		return center_html::Escape(seg);
	};

	if (edited)
	{
		if (upOn && downOn)
		{
			std::string keys = FillTemplate(footerRangeFormat, {{"up", EffectiveNavLabel(*def, slot, MenuNavAction::Up)},
																{"down", EffectiveNavLabel(*def, slot, MenuNavAction::Down)}});
			addSegment(hint(MenuLabel::Adjust, keys));
		}
		else if (upOn || downOn)
		{
			addSegment(hint(MenuLabel::Adjust, EffectiveNavLabel(*def, slot, upOn ? MenuNavAction::Up : MenuNavAction::Down)));
		}
		// Back leaves the edit even on a menu that can't be exited.
		const bool backKey = EffectiveNavMask(*def, slot, MenuNavAction::Back) != 0;
		if (selectOn || backKey)
		{
			addSegment(hint(MenuLabel::Done, EffectiveNavLabel(*def, slot, selectOn ? MenuNavAction::Select : MenuNavAction::Back)));
		}
	}
	else if (upOn && downOn)
	{
		std::string keys = FillTemplate(footerRangeFormat, {{"up", EffectiveNavLabel(*def, slot, MenuNavAction::Up)},
															{"down", EffectiveNavLabel(*def, slot, MenuNavAction::Down)}});
		addSegment(hint(MenuLabel::Move, keys));
	}
	else if (downOn)
	{
		addSegment(hint(MenuLabel::Scroll, EffectiveNavLabel(*def, slot, MenuNavAction::Down)));
	}
	else if (upOn)
	{
		addSegment(hint(MenuLabel::Scroll, EffectiveNavLabel(*def, slot, MenuNavAction::Up)));
	}
	if (selectOn && !edited)
	{
		addSegment(hint(MenuLabel::Select, EffectiveNavLabel(*def, slot, MenuNavAction::Select)));
	}
	if (backOn && !edited)
	{
		addSegment(hint(MenuLabel::Exit, EffectiveNavLabel(*def, slot, MenuNavAction::Back)));
	}

	if (showFooter && !footer.empty())
	{
		html += "<font color='";
		html += footerColor;
		html += "' class='" + footerCls + "'>";
		html += footer;
		html += "</font>";
	}

	SendHtml(slot, html);
}

void MenuManager::SendHtml(int slot, const std::string &html)
{
	PlayerMenu &pm = m_players[slot];
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

// The first character, uppercased when ASCII, for "A - D" style page labels.
// With a delimiter it skips a leading prefix of up to 5 letters or digits ending in it, see MenuStyle::PagePrefixDelimiter.
static std::string IndexLetter(const std::string &text, char delimiter)
{
	size_t skip = 0;
	for (size_t i = 0; delimiter && i < text.size() && i <= 5; i++)
	{
		if (text[i] == delimiter)
		{
			skip = i > 0 ? i + 1 : 0;
			break;
		}
		if (!isalnum(static_cast<unsigned char>(text[i])))
		{
			break;
		}
	}
	if (skip >= text.size())
	{
		return std::string();
	}
	size_t n = skip + 1;
	while (n < text.size() && (static_cast<unsigned char>(text[n]) & 0xC0) == 0x80)
	{
		n++;
	}
	std::string letter = text.substr(skip, n - skip);
	if (letter.size() == 1)
	{
		letter[0] = static_cast<char>(toupper(static_cast<unsigned char>(letter[0])));
	}
	return letter;
}

std::vector<MenuManager::IndexRange> MenuManager::IndexRanges(const MenuDef &def, MenuType type) const
{
	const int htmlVisible = def.style.visibleItems > 0 ? (std::min)(def.style.visibleItems, MENU_MAX_HTML_VISIBLE) : m_htmlVisibleItems;
	const int size = type == MenuType::Chat ? m_itemsPerPage : type == MenuType::Html ? htmlVisible : 0;
	const int count = static_cast<int>(def.items.size());
	if (!(def.textFeatures & kMenuTextIndex) || !def.sections.empty() || size < 2 || count < kIndexMinPages * size)
	{
		return {};
	}

	std::vector<int> starts;
	std::vector<std::string> letters;
	for (int i = 0; i < count; i++)
	{
		if (OffPage(def, type, i))
		{
			continue;
		}
		std::string letter = IndexLetter(panorama_hud::StripColors(def.items[i].text), def.style.pagePrefixDelimiter);
		if (letters.empty() || letter != letters.back())
		{
			starts.push_back(i);
			letters.push_back(std::move(letter));
		}
	}

	// Each even share of the list starts at the letter change nearest to it.
	std::vector<size_t> cuts = {0};
	for (int share = 1; share < size; share++)
	{
		const int even = count * share / size;
		auto at = std::lower_bound(starts.begin(), starts.end(), even);
		if (at == starts.end() || (at != starts.begin() && even - *(at - 1) < *at - even))
		{
			--at;
		}
		const size_t cut = static_cast<size_t>(at - starts.begin());
		if (cut > cuts.back())
		{
			cuts.push_back(cut);
		}
	}
	if (cuts.size() < 2)
	{
		return {};
	}

	std::vector<IndexRange> ranges;
	for (size_t i = 0; i < cuts.size(); i++)
	{
		const std::string &from = letters[cuts[i]];
		const std::string &to = letters[(i + 1 < cuts.size() ? cuts[i + 1] : letters.size()) - 1];
		ranges.push_back({starts[cuts[i]], from == to ? from : from + " - " + to});
	}
	return ranges;
}

bool MenuManager::EnterIndex(int slot)
{
	PlayerMenu &pm = m_players[slot];
	const MenuDef *def = Find(pm.handle);
	if (!def || pm.index)
	{
		return false;
	}
	const std::vector<IndexRange> ranges = IndexRanges(*def, pm.type);
	if (ranges.empty())
	{
		return false;
	}
	int range = 0;
	while (range + 1 < static_cast<int>(ranges.size()) && ranges[range + 1].first <= pm.cursor)
	{
		range++;
	}
	pm.index = true;
	pm.cursor = range;
	return true;
}

void MenuManager::OpenRange(int slot, int range)
{
	PlayerMenu &pm = m_players[slot];
	const MenuDef *def = Find(pm.handle);
	const std::vector<IndexRange> ranges = def ? IndexRanges(*def, pm.type) : std::vector<IndexRange>();
	if (range < 0 || range >= static_cast<int>(ranges.size()))
	{
		return;
	}
	pm.index = false;
	pm.cursor = ranges[range].first;
	pm.page = PageOf(*def, pm.type, pm.cursor);
	Render(slot);
}

std::vector<MenuManager::TabEntry> MenuManager::Tabs(int slot, const MenuDef &def, const std::vector<Page> &pages, int page) const
{
	std::vector<TabEntry> tabs;
	if (!def.tabs.empty())
	{
		for (int i = 0; i < static_cast<int>(def.tabs.size()); i++)
		{
			const MenuDef::Tab &tab = def.tabs[i];
			tabs.push_back({panorama_hud::StripColors(tab.label), tab.selected, tab.marked, tab.pinned, i});
		}
		return tabs;
	}
	// The columns show every section at once.
	if (def.sections.empty() || PanoramaLayout(def) == panorama_hud::Layout::Columns)
	{
		return tabs;
	}
	const int current = pages[page].section;
	for (int p = 0; p < static_cast<int>(pages.size()); p++)
	{
		if (p > 0 && pages[p].section == pages[p - 1].section)
		{
			continue;
		}
		const int section = pages[p].section;
		std::string label;
		if (section >= 0)
		{
			label = panorama_hud::StripColors(def.sections[section]);
		}
		else
		{
			const std::string format = g_Translations.Translate(Lang(slot), "Page {n}");
			label = FillTemplate(format, {{"n", std::to_string(tabs.size() + 1)}});
		}
		tabs.push_back({std::move(label), section == current, false, false, p});
	}
	// One tab switches nothing. A studio picker's items also sit under its control panel's last section.
	if (tabs.size() == 1)
	{
		tabs.clear();
	}
	return tabs;
}

std::vector<int> MenuManager::FitTabs(panorama_hud::Layout layout, const std::vector<TabEntry> &tabs,
									  const std::vector<panorama_hud::View::Chip> &chips, bool paged)
{
	const int count = static_cast<int>(tabs.size());
	const int slots = panorama_hud::NavSlots(layout);
	// The studio's row wraps, only the buttons run out there. The page arrows share the row, and the columns' chips.
	int room = layout == panorama_hud::Layout::Studio ? INT_MAX : kTabRow;
	room -= paged && layout != panorama_hud::Layout::Studio ? kTabPager : 0;
	for (size_t i = 0; layout == panorama_hud::Layout::Columns && i < chips.size(); i++)
	{
		const panorama_hud::View::Chip &chip = chips[i];
		room -= kChipPadding + (chip.menu ? kChipCaret : 0)
				+ kChipCell * (panorama_hud::TextCells(chip.label) + (chip.value.empty() ? 0 : panorama_hud::TextCells(chip.value) + 1));
	}
	auto width = [&tabs](int i) { return kTabPadding + (tabs[i].marked ? kTabDot : 0) + kTabCell * panorama_hud::TextCells(tabs[i].label); };
	long long total = 0;
	for (int i = 0; i < count; i++)
	{
		total += width(i);
	}
	std::vector<int> fits;
	if (count <= slots && total <= room)
	{
		for (int i = 0; i < count; i++)
		{
			fits.push_back(i);
		}
		return fits;
	}
	// The selected one and the pinned stay whatever happens, the others in order while there's room beside "+N".
	std::vector<bool> kept(count, false);
	long long used = kTabMore;
	int left = slots - 1;
	for (int i = 0; i < count && left > 0; i++)
	{
		if (tabs[i].selected || tabs[i].pinned)
		{
			kept[i] = true;
			used += width(i);
			left--;
		}
	}
	for (int i = 0; i < count && left > 0; i++)
	{
		if (kept[i])
		{
			continue;
		}
		if (used + width(i) > room)
		{
			break;
		}
		kept[i] = true;
		used += width(i);
		left--;
	}
	for (int i = 0; i < count; i++)
	{
		if (kept[i])
		{
			fits.push_back(i);
		}
	}
	return fits;
}

void MenuManager::PickTab(int slot, int tab)
{
	PlayerMenu &pm = m_players[slot];
	const MenuHandle handle = pm.handle;
	const MenuDef *def = Find(handle);
	if (!def)
	{
		return;
	}
	const std::vector<Page> pages = Pages(*def, pm.type);
	const std::vector<TabEntry> tabs = Tabs(slot, *def, pages, (std::max)(0, (std::min)(pm.page, static_cast<int>(pages.size()) - 1)));
	if (tab < 0 || tab >= static_cast<int>(tabs.size()))
	{
		return;
	}
	pm.editItem = -1;
	pm.editPage = 0;
	if (def->tabs.empty())
	{
		pm.page = tabs[tab].target;
		RenderPanorama(slot);
		return;
	}
	MenuTabFn onTab = def->onTab;
	{
		DepthGuard guard(m_callbackDepth);
		if (onTab && guard.enter())
		{
			onTab(handle, slot, tabs[tab].target);
		}
	}
	// The plugin usually shows another menu. When it didn't, the list of tabs still closes.
	if (pm.active && !pm.suspended && pm.handle == handle && Find(handle))
	{
		RenderPanorama(slot);
	}
}

void MenuManager::RenderPanorama(int slot)
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

	const std::vector<Page> pages = Pages(*def, pm.type);
	const int pageCount = static_cast<int>(pages.size());
	// A live item removal can leave the page past the end.
	pm.page = (std::max)(0, (std::min)(pm.page, pageCount - 1));
	const Page &current = pages[pm.page];

	// Per-menu color overrides still apply, the rest of MenuStyle is HTML layout.
	const StyleOverride &st = def->style;
	auto pick = [](const std::string &override_, const std::string &dflt) -> const std::string & { return override_.empty() ? dflt : override_; };
	const std::string &titleColor = pick(st.titleColor, m_settings.panoramaTitleColor);
	const std::string &itemColor = pick(st.itemColor, m_settings.panoramaItemColor);
	const std::string &disabledColor = pick(st.disabledColor, m_settings.panoramaDisabledColor);
	panorama_hud::View view;
	view.layout = PanoramaLayout(*def);
	view.tiles = TileSize(*def);
	view.collapsed = pm.collapsed;
	view.turning = pm.turning && view.layout == panorama_hud::Layout::Studio;
	if (pm.messageUntil > 0.0f)
	{
		view.message = pm.message;
		view.messageTone = static_cast<int>(pm.messageTone);
	}
	if (def->edited)
	{
		view.edited = g_Translations.Translate(Lang(slot), "Edited");
	}
	view.scope = panorama_hud::StripColors(def->scope);
	view.scopeTeams = def->scopeTeams;
	view.scopeButton = static_cast<bool>(def->onScope);
	if (pm.dialog.open)
	{
		view.dialog = {true, pm.dialog.title, pm.dialog.body, pm.dialog.cancel, pm.dialog.confirm, pm.dialog.danger};
	}
	// The first Input item is the field, typing shows it even without one.
	if (view.layout != panorama_hud::Layout::List)
	{
		int field = -1;
		for (int i = 0; i < static_cast<int>(def->items.size()) && field < 0; i++)
		{
			field = def->items[i].role == MenuItemRole::Input ? i : -1;
		}
		const bool typing = pm.inputMenu == pm.handle;
		if (field >= 0 || typing)
		{
			view.input.shown = true;
			view.input.overlay = field < 0;
			view.input.typing = typing;
			if (typing)
			{
				view.input.text = panorama_hud::StripColors(pm.inputPrompt);
				view.input.hint = panorama_hud::StripColors(pm.inputHint);
			}
			else
			{
				const MenuItem &item = def->items[field];
				view.input.placeholder = item.text.empty();
				view.input.text = panorama_hud::StripColors(item.text.empty() ? item.subtext : item.text);
				view.input.hint = item.text.empty() ? std::string() : panorama_hud::StripColors(item.subtext);
				view.input.clear = !item.text.empty() && static_cast<bool>(def->onInputClear);
			}
		}
	}
	if ((view.layout == panorama_hud::Layout::Showcase || view.layout == panorama_hud::Layout::Studio) && !def->info.title.empty())
	{
		const MenuInfo &info = def->info;
		auto percent = [](float value) { return static_cast<int>(std::lround((std::max)(0.0f, (std::min)(1.0f, value)) * 100.0f)); };
		view.info.shown = true;
		view.info.title = panorama_hud::StripColors(info.title);
		view.info.subtitle = panorama_hud::StripColors(info.subtitle);
		view.info.subtitleColor = info.subtitleColor;
		view.info.meter = info.meter >= 0.0f;
		view.info.mark = percent(info.meter);
		view.info.rangeLo = percent(info.rangeMin);
		view.info.rangeHi = percent(info.rangeMax);
		// Each band's width, from its upper end.
		int from = 0;
		for (size_t band = 0; band < info.bands.size() && band < static_cast<size_t>(panorama_hud::kInfoBands); band++)
		{
			const int to = percent(info.bands[band]);
			view.info.bands.push_back((std::max)(0, to - from));
			from = (std::max)(from, to);
		}
		view.info.meterLabel = panorama_hud::StripColors(info.meterLabel);
		view.info.meterValue = panorama_hud::StripColors(info.meterValue);
		// A pair too long for half the card's line takes a whole one, after the short ones. The studio's card is the narrower.
		const int half = view.layout == panorama_hud::Layout::Studio ? 17 : 22;
		std::vector<panorama_hud::View::Info::Row> wide;
		for (size_t i = 0; i < info.rows.size() && i < static_cast<size_t>(panorama_hud::kInfoRows); i++)
		{
			panorama_hud::View::Info::Row row {panorama_hud::StripColors(info.rows[i].first), panorama_hud::StripColors(info.rows[i].second)};
			row.wide = panorama_hud::TextCells(row.label) + panorama_hud::TextCells(row.value) > half;
			(row.wide ? wide : view.info.rows).push_back(std::move(row));
		}
		view.info.rows.insert(view.info.rows.end(), wide.begin(), wide.end());
	}
	view.image = def->image;
	view.fontClass = m_settings.panoramaFontClass;
	view.sounds = m_settings.panoramaSounds;
	// The layout only takes plain text, so raw markup shows as typed.
	view.title = panorama_hud::StripColors(def->title);
	view.titleColor = titleColor;
	view.navColor = itemColor;
	view.closeButton = def->exitButton;
	view.backButton = HasBack(slot);
	view.forwardButton = HasForward(slot);
	view.refreshButton = static_cast<bool>(def->onRefresh);

	const int pinned = PinnedItem(*def, pm.type);
	if (pinned >= 0)
	{
		const MenuItem &item = def->items[pinned];
		view.action = panorama_hud::StripColors(item.text);
		view.actionColor = item.disabled ? disabledColor : itemColor;
		view.actionDisabled = item.disabled;
	}
	if (const int secondary = SecondaryItem(*def, pm.type); secondary >= 0)
	{
		view.action2 = panorama_hud::StripColors(def->items[secondary].text);
		view.action2Disabled = def->items[secondary].disabled;
	}
	if (view.layout == panorama_hud::Layout::Studio)
	{
		std::vector<std::string> tabs;
		const std::vector<int> controls = StudioControls(*def, pm.controlTab, tabs);
		pm.controlSlots.clear();
		pm.controlOptions.clear();
		for (int i : controls)
		{
			const MenuItem &item = def->items[i];
			// A Choice drawn in place takes a slot per option.
			const int segments = Segments(item);
			for (int option = 0; option < (std::max)(segments, 1) && static_cast<int>(view.controls.size()) < panorama_hud::kStudioControls; option++)
			{
				panorama_hud::View::ControlButton control;
				if (segments > 0)
				{
					control.label = panorama_hud::StripColors(OptionLabel(item.options[option]));
					control.segCount = segments;
					control.segIndex = option;
					control.highlight = option == item.value;
					control.disabled = item.disabled;
				}
				else
				{
					control.heading = item.role == MenuItemRole::Heading;
					control.readout = item.role == MenuItemRole::Readout;
					control.label = panorama_hud::StripColors(control.readout ? item.text : LineText(item));
					control.sub = control.readout ? panorama_hud::StripColors(item.subtext) : std::string();
					control.disabled = control.readout ? item.disabled : Inert(item);
					control.highlight = item.highlight;
					control.span = control.heading ? 3 : item.span;
				}
				view.controls.push_back(std::move(control));
				pm.controlSlots.push_back(i);
				pm.controlOptions.push_back(segments > 0 ? option : -1);
			}
		}
		const bool known = std::find(tabs.begin(), tabs.end(), pm.controlTab) != tabs.end();
		for (int i = 0; i < static_cast<int>(tabs.size()) && i < panorama_hud::kStudioControlTabs; i++)
		{
			view.controlTabs.push_back({panorama_hud::StripColors(tabs[i]), known ? tabs[i] == pm.controlTab : i == 0});
		}
		// The pill: how to stop while turning, else the plugin's parts or how to start, or none when the plugin hid it.
		const std::string lang = Lang(slot);
		auto part = [](std::vector<panorama_hud::View::HintPart> &parts, const std::string &keys, const std::string &text)
		{
			panorama_hud::View::HintPart p;
			std::string key;
			for (char c : keys + " ")
			{
				if (c != ' ')
				{
					key += c;
				}
				else if (!key.empty())
				{
					p.keys.push_back(std::move(key));
					key.clear();
				}
			}
			p.text = panorama_hud::StripColors(text);
			parts.push_back(std::move(p));
		};
		if (view.turning)
		{
			part(view.hint, "LMB", g_Translations.Translate(lang, "Stop turning"));
		}
		else if (pm.hint.empty() && !pm.hintHidden)
		{
			part(view.hint, "LMB", g_Translations.Translate(lang, "Turn the view"));
		}
		else
		{
			for (const auto &[keys, text] : pm.hint)
			{
				part(view.hint, keys, text);
			}
		}
		for (const auto &[keys, text] : pm.help)
		{
			part(view.help, keys, text);
		}
		view.helpOpen = pm.helpOpen;
		view.helpTitle = g_Translations.Translate(lang, "Keys");
		view.mirrored = pm.mirrored;
	}
	const bool columns = view.layout == panorama_hud::Layout::Columns;
	const bool buttons = view.layout == panorama_hud::Layout::Showcase || view.layout == panorama_hud::Layout::Studio;
	int columnUsed[panorama_hud::kColumns] = {};
	pm.panoramaSlots.assign(columns ? panorama_hud::kColumnSlots : 0, -1);
	pm.panoramaOptions.clear();
	for (int i = current.first; i < current.end; i++)
	{
		if (OffPage(*def, pm.type, i))
		{
			continue;
		}
		const MenuItem &item = def->items[i];
		// A Choice drawn in place: a button per option, the picked one lit.
		if (const int segments = buttons ? Segments(item) : 0; segments > 0)
		{
			for (int option = 0; option < segments; option++)
			{
				panorama_hud::View::Row row;
				row.segments = panorama_hud::SplitColors(OptionLabel(item.options[option]), item.disabled ? disabledColor : itemColor);
				row.disabled = item.disabled;
				row.segCount = segments;
				row.segIndex = option;
				row.highlight = option == item.value;
				view.rows.push_back(std::move(row));
				pm.panoramaSlots.push_back(i);
				pm.panoramaOptions.push_back(option);
			}
			continue;
		}
		panorama_hud::View::Row row;
		row.segments = panorama_hud::SplitColors(LineText(item), Inert(item) ? disabledColor : itemColor);
		if (item.submenu != kInvalidMenuHandle)
		{
			row.value = "\xE2\x80\xBA"; // ›
		}
		row.disabled = Inert(item);
		row.image = item.image;
		row.imageTint = item.imageTint;
		row.rarity = item.rarity;
		row.tag = panorama_hud::StripColors(item.tag);
		row.tagStyle = item.tagStyle;
		row.teams = item.teams;
		row.locked = item.locked;
		row.corner = static_cast<panorama_hud::View::Corner>(item.corner);
		row.highlight = item.highlight;
		switch (item.type)
		{
			case MenuItemType::Toggle:
				row.control = panorama_hud::View::Control::Toggle;
				row.on = item.value != 0;
				break;
			case MenuItemType::Stepper:
				row.control = panorama_hud::View::Control::Stepper;
				break;
			case MenuItemType::Choice:
				row.control = panorama_hud::View::Control::Choice;
				break;
			default:
				break;
		}
		if (const std::string suffix = SuffixText(slot, *def, item); !suffix.empty())
		{
			row.value = panorama_hud::StripColors(suffix);
		}
		if (columns)
		{
			// Its section is its column, in the order added.
			const int column = (std::max)(item.section, 0);
			row.heading = item.role == MenuItemRole::Heading;
			row.half = item.span < 2 && !row.heading;
			if (row.heading)
			{
				row.segments = panorama_hud::SplitColors(item.text, itemColor);
				row.value.clear();
			}
			if (column < panorama_hud::kColumns)
			{
				if (static_cast<int>(view.columns.size()) <= column)
				{
					view.columns.resize(column + 1);
				}
				int &used = columnUsed[column];
				if (used < panorama_hud::kColumnRows)
				{
					row.slot = column * panorama_hud::kColumnRows + used++;
					pm.panoramaSlots[row.slot] = i;
				}
				view.columns[column].count += row.heading ? 0 : 1;
			}
		}
		if (buttons)
		{
			row.span = item.span;
			// Its label over its value, where the other layouts list "label: value".
			if (item.role == MenuItemRole::Readout)
			{
				row.readout = true;
				row.disabled = item.disabled;
				row.segments = panorama_hud::SplitColors(item.text, itemColor);
				row.value = panorama_hud::StripColors(item.subtext);
			}
			else if (item.role == MenuItemRole::Heading)
			{
				row.heading = true;
				row.span = 3;
				row.image.clear();
				row.segments = panorama_hud::SplitColors(item.text, itemColor);
				row.value.clear();
			}
			pm.panoramaSlots.push_back(i);
			pm.panoramaOptions.push_back(-1);
		}
		view.rows.push_back(std::move(row));
	}

	// The list popup open on a page of `count` rows, `rowOf` making each.
	auto fillList = [&](const std::string &title, int count, auto rowOf)
	{
		view.list.open = true;
		view.list.title = title;
		const int listPages = (std::max)(1, (count + panorama_hud::kListSlots - 1) / panorama_hud::kListSlots);
		pm.editPage = (std::max)(0, (std::min)(pm.editPage, listPages - 1));
		const int first = pm.editPage * panorama_hud::kListSlots;
		for (int i = first; i < (std::min)(first + panorama_hud::kListSlots, count); i++)
		{
			view.list.rows.push_back(rowOf(i));
		}
		if (listPages > 1)
		{
			view.list.page = std::to_string(pm.editPage + 1) + "/" + std::to_string(listPages);
			view.list.prev = pm.editPage > 0;
			view.list.next = pm.editPage + 1 < listPages;
		}
	};
	for (const MenuDef::Chip &chip : def->chips)
	{
		panorama_hud::View::Chip c;
		c.label = panorama_hud::StripColors(chip.label);
		if (chip.note)
		{
			c.note = true;
			c.value = panorama_hud::StripColors(chip.value);
			view.chips.push_back(std::move(c));
			continue;
		}
		c.menu = !chip.options.empty();
		c.on = c.menu && !chip.action ? chip.selected >= 0 : chip.selected > 0;
		if (c.menu && c.on && !chip.action)
		{
			c.value = panorama_hud::StripColors(OptionLabel(chip.options[chip.selected]));
		}
		view.chips.push_back(std::move(c));
	}
	if (const int chip = -2 - pm.editItem; chip >= 0 && chip < static_cast<int>(def->chips.size()) && !def->chips[chip].options.empty())
	{
		const MenuDef::Chip &edited = def->chips[chip];
		fillList(panorama_hud::StripColors(edited.label), static_cast<int>(edited.options.size()),
				 [&edited](int i)
				 {
					 panorama_hud::View::ListRow row;
					 row.label = panorama_hud::StripColors(OptionLabel(edited.options[i]));
					 row.sub = panorama_hud::StripColors(OptionSub(edited.options[i]));
					 row.selected = !edited.action && i == edited.selected;
					 row.tone = i < static_cast<int>(edited.tones.size()) ? static_cast<int>(edited.tones[i]) : 0;
					 return row;
				 });
	}
	for (int c = 0; c < static_cast<int>(view.columns.size()); c++)
	{
		view.columns[c].label = c < static_cast<int>(def->sections.size()) ? panorama_hud::StripColors(def->sections[c]) : std::string();
	}
	if (view.rows.empty() && !def->emptyTitle.empty())
	{
		view.emptyTitle = panorama_hud::StripColors(def->emptyTitle);
		view.emptyText = panorama_hud::StripColors(def->emptyText);
		view.emptyLoading = def->emptyLoading;
	}
	if (const MenuItem *edited = EditedItem(slot))
	{
		const std::string title = panorama_hud::StripColors(edited->text);
		if (edited->type == MenuItemType::Stepper)
		{
			view.step.open = true;
			view.step.title = title;
			view.step.readout = std::to_string(edited->value);
			const auto counts = StepCounts(*edited);
			for (int b = 0; b < panorama_hud::kStepButtons; b++)
			{
				if (counts[b] == 0)
				{
					continue;
				}
				panorama_hud::View::StepButton &button = view.step.buttons[b];
				button.label = (counts[b] > 0 ? "+" : "") + std::to_string(static_cast<long long>(counts[b]) * edited->step);
				button.enabled = counts[b] < 0 ? edited->value > edited->min : edited->value < edited->max;
			}
		}
		else
		{
			fillList(title, static_cast<int>(edited->options.size()),
					 [edited](int i)
					 {
						 panorama_hud::View::ListRow row;
						 row.label = panorama_hud::StripColors(OptionLabel(edited->options[i]));
						 row.sub = panorama_hud::StripColors(OptionSub(edited->options[i]));
						 row.selected = i == edited->value;
						 return row;
					 });
		}
	}

	pm.panoramaNav.clear();
	auto addNav = [&](int page, const std::string &label, bool selected)
	{
		view.nav.push_back({label, selected});
		pm.panoramaNav.push_back(page);
	};
	const std::string lang = Lang(slot);
	const std::string pageFormat = g_Translations.Translate(lang, "Page {n}");
	auto sectionLabel = [&](int section, int n)
	{ return section >= 0 ? panorama_hud::StripColors(def->sections[section]) : FillTemplate(pageFormat, {{"n", std::to_string(n)}}); };

	if (view.layout != panorama_hud::Layout::List)
	{
		// Arrows: the current section's pages.
		int sectionFirst = pm.page;
		while (sectionFirst > 0 && pages[sectionFirst - 1].section == current.section)
		{
			sectionFirst--;
		}
		int sectionEnd = pm.page + 1;
		while (sectionEnd < pageCount && pages[sectionEnd].section == current.section)
		{
			sectionEnd++;
		}
		if (sectionEnd - sectionFirst > 1)
		{
			view.page = std::to_string(pm.page - sectionFirst + 1) + "/" + std::to_string(sectionEnd - sectionFirst);
			view.prev = pm.page > sectionFirst;
			view.next = pm.page + 1 < sectionEnd;
		}
		// Tabs: the plugin's own, else one per section. The ones the row has no room for go behind "+N".
		const std::vector<TabEntry> tabs = Tabs(slot, *def, pages, pm.page);
		const std::vector<int> fits = FitTabs(view.layout, tabs, view.chips, !view.page.empty());
		const int hidden = static_cast<int>(tabs.size() - fits.size());
		// "+N" follows the last tab that isn't pinned, so it sits before one that adds a tab.
		int moreAfter = -1;
		for (int k = 0; hidden > 0 && k < static_cast<int>(fits.size()); k++)
		{
			moreAfter = tabs[fits[k]].pinned ? moreAfter : k;
		}
		auto addMore = [&]
		{
			view.navMore = static_cast<int>(view.nav.size());
			view.nav.push_back({"+" + std::to_string(hidden), pm.editItem == kEditTabs});
			pm.panoramaNav.push_back(kNavMore);
		};
		if (hidden > 0 && moreAfter < 0)
		{
			addMore();
		}
		for (int k = 0; k < static_cast<int>(fits.size()); k++)
		{
			const TabEntry &tab = tabs[fits[k]];
			view.nav.push_back({tab.label, tab.selected, tab.marked});
			pm.panoramaNav.push_back(fits[k]);
			if (hidden > 0 && k == moreAfter)
			{
				addMore();
			}
		}
		// Every tab in the list popup, a pick there as good as a click on the tab.
		if (pm.editItem == kEditTabs && hidden == 0)
		{
			pm.editItem = -1;
		}
		if (pm.editItem == kEditTabs)
		{
			fillList(view.title, static_cast<int>(tabs.size()),
					 [&tabs](int i)
					 {
						 panorama_hud::View::ListRow row;
						 row.label = tabs[i].label;
						 row.selected = tabs[i].selected;
						 row.marked = tabs[i].marked;
						 return row;
					 });
		}
	}
	// Left column: every page, or a window around the current one when they don't fit.
	else if (pageCount > 1)
	{
		// Section names, else first to last letter per page. Numbered when pages share one, like "B (1)".
		std::vector<std::string> labels(pageCount);
		std::map<std::string, int> rangeTotals;
		for (int page = 0; page < pageCount; page++)
		{
			if (!def->sections.empty())
			{
				labels[page] = sectionLabel(pages[page].section, page + 1);
			}
			else
			{
				const std::string from = IndexLetter(panorama_hud::StripColors(def->items[pages[page].first].text), st.pagePrefixDelimiter);
				const std::string to = IndexLetter(panorama_hud::StripColors(def->items[pages[page].end - 1].text), st.pagePrefixDelimiter);
				labels[page] = from.empty() || to.empty() ? std::string() : (from == to ? from : from + " - " + to);
			}
			if (!labels[page].empty())
			{
				rangeTotals[labels[page]]++;
			}
		}
		std::map<std::string, int> rangeSeen;
		for (std::string &label : labels)
		{
			if (!label.empty() && rangeTotals[label] > 1)
			{
				label += " (" + std::to_string(++rangeSeen[label]) + ")";
			}
		}
		auto pageLabel = [&](int page) { return labels[page].empty() ? FillTemplate(pageFormat, {{"n", std::to_string(page + 1)}}) : labels[page]; };

		// The arrows jump a whole window.
		const int window = pageCount <= panorama_hud::kNavSlots ? pageCount : panorama_hud::kNavSlots - 2;
		const int start = (std::max)(0, (std::min)(pm.page - window / 2, pageCount - window));
		if (start > 0)
		{
			addNav((std::max)(0, pm.page - window), ResolveLabel(slot, *def, MenuLabel::PrevPage), false);
		}
		for (int page = start; page < start + window; page++)
		{
			addNav(page, pageLabel(page), page == pm.page);
		}
		if (start + window < pageCount)
		{
			addNav((std::min)(pageCount - 1, pm.page + window), ResolveLabel(slot, *def, MenuLabel::NextPage), false);
		}
	}

	if (!panorama_hud::Show(slot, view))
	{
		// No window for this player: fall back so the menu stays usable.
		pm.type = m_htmlAvailable ? MenuType::Html : MenuType::Chat;
		pm.page = 0;
		EnterIndex(slot);
		Render(slot);
	}
}

void MenuManager::RedrawPanorama(int slot)
{
	const PlayerMenu &pm = m_players[slot];
	if (pm.active && !pm.suspended && pm.type == MenuType::Panorama && Find(pm.handle))
	{
		RenderPanorama(slot);
	}
}

int MenuManager::ClickedItem(int slot, int index) const
{
	const PlayerMenu &pm = m_players[slot];
	// Columns, showcase and studio keep which item each slot drew. The list and the grid draw a page in order.
	if (!pm.panoramaSlots.empty())
	{
		return index >= 0 && index < static_cast<int>(pm.panoramaSlots.size()) ? pm.panoramaSlots[index] : -1;
	}
	const MenuDef *shown = Find(pm.handle);
	if (!shown)
	{
		return -1;
	}
	const std::vector<Page> pages = Pages(*shown, pm.type);
	return pm.page < static_cast<int>(pages.size()) ? PageItem(*shown, pm.type, pages[pm.page], index) : -1;
}

void MenuManager::OnPanoramaClick(int slot, panorama_hud::Click click, int index, float curtime)
{
	ScopedLock lock(m_mutex);
	if (!ValidSlot(slot))
	{
		return;
	}
	// Selection callbacks are main-thread only.
	if (!OnMainThread())
	{
		m_pending.push_back([this, slot, click, index, curtime] { OnPanoramaClick(slot, click, index, curtime); });
		return;
	}
	if (click == panorama_hud::Click::Notice)
	{
		// A copy, the callback may show or hide the notice.
		MenuNoticeFn onClick = m_notices[slot].onClick;
		DepthGuard guard(m_callbackDepth);
		if (onClick && guard.enter())
		{
			onClick(slot);
		}
		return;
	}
	PlayerMenu &pm = m_players[slot];
	if (!pm.active || pm.suspended || pm.type != MenuType::Panorama || !Find(pm.handle))
	{
		return;
	}
	m_curtime = curtime;

	if (pm.dialog.open)
	{
		if (click == panorama_hud::Click::DialogYes || click == panorama_hud::Click::DialogNo)
		{
			MenuConfirmFn onDone = std::move(pm.dialog.onDone);
			pm.dialog = {};
			{
				DepthGuard guard(m_callbackDepth);
				if (onDone && guard.enter())
				{
					onDone(slot, click == panorama_hud::Click::DialogYes);
				}
			}
			RedrawPanorama(slot);
		}
		return;
	}

	// A click calls off typing first. On the field that's all it does.
	if (pm.inputMenu != kInvalidMenuHandle)
	{
		const MenuHandle typing = std::exchange(pm.inputMenu, kInvalidMenuHandle);
		MenuInputCancelFn onCancel = std::move(pm.onInputCancel);
		pm.onInputCancel = nullptr;
		{
			DepthGuard guard(m_callbackDepth);
			if (onCancel && typing == pm.handle && guard.enter())
			{
				onCancel(typing, slot);
			}
		}
		if (click == panorama_hud::Click::Input)
		{
			if (pm.active && Find(pm.handle))
			{
				RenderPanorama(slot);
			}
			return;
		}
		if (!pm.active || !Find(pm.handle))
		{
			return;
		}
	}

	switch (click)
	{
		case panorama_hud::Click::Close:
		{
			// The window's own X closes the whole display, history included. Back has its own button here.
			pm.editItem = -1;
			const MenuDef *shown = Find(pm.handle);
			if (shown && shown->exitButton && !GuardLeave(slot, true, [this, slot] { EndDisplay(slot, MenuEndReason::Exit); }))
			{
				EndDisplay(slot, MenuEndReason::Exit);
			}
			break;
		}
		case panorama_hud::Click::Back:
			pm.editItem = -1;
			if (!GuardLeave(slot, false, [this, slot] { StepBack(slot); }))
			{
				StepBack(slot);
			}
			break;
		case panorama_hud::Click::Forward:
			pm.editItem = -1;
			StepForward(slot);
			break;
		case panorama_hud::Click::Collapse:
			pm.editItem = -1;
			pm.collapsed = !pm.collapsed;
			Render(slot);
			break;
		case panorama_hud::Click::Refresh:
		{
			pm.editItem = -1;
			// The owner rebuilds the page, usually through ReplaceMenu.
			const MenuDef *shown = Find(pm.handle);
			MenuRefreshFn onRefresh = shown ? shown->onRefresh : nullptr;
			DepthGuard guard(m_callbackDepth);
			if (onRefresh && guard.enter())
			{
				onRefresh(pm.handle, slot);
			}
			break;
		}
		case panorama_hud::Click::PopupClose:
			StopEdit(slot);
			break;
		case panorama_hud::Click::Step:
		{
			const MenuItem *edited = EditedItem(slot);
			if (edited && edited->type == MenuItemType::Stepper && index >= 0 && index < panorama_hud::kStepButtons)
			{
				const int count = StepCounts(*edited)[index];
				if (count != 0)
				{
					StepValue(slot, pm.editItem, count);
				}
			}
			break;
		}
		case panorama_hud::Click::InputClear:
		{
			const MenuHandle handle = pm.handle;
			MenuInputClearFn onClear = Find(handle)->onInputClear;
			DepthGuard guard(m_callbackDepth);
			if (onClear && guard.enter())
			{
				pm.editItem = -1;
				onClear(handle, slot);
			}
			break;
		}
		case panorama_hud::Click::Scope:
		{
			const MenuHandle handle = pm.handle;
			MenuScopeFn onScope = Find(handle)->onScope;
			DepthGuard guard(m_callbackDepth);
			if (onScope && guard.enter())
			{
				pm.editItem = -1;
				onScope(handle, slot);
			}
			break;
		}
		case panorama_hud::Click::Input:
		{
			const MenuDef &shown = *Find(pm.handle);
			for (int i = 0; i < static_cast<int>(shown.items.size()); i++)
			{
				if (shown.items[i].role == MenuItemRole::Input)
				{
					pm.editItem = -1;
					Select(slot, i);
					break;
				}
			}
			break;
		}
		case panorama_hud::Click::Chip:
		{
			const MenuDef &shown = *Find(pm.handle);
			if (index < 0 || index >= static_cast<int>(shown.chips.size()))
			{
				break;
			}
			const MenuDef::Chip &chip = shown.chips[index];
			if (chip.note)
			{
				break;
			}
			if (chip.options.empty())
			{
				pm.editItem = -1;
				PickChip(slot, index, chip.action ? 0 : chip.selected > 0 ? 0 : 1);
				break;
			}
			// A second click closes its list.
			const bool open = pm.editItem == -2 - index;
			pm.editItem = open ? -1 : -2 - index;
			pm.editPage = open || chip.action || chip.selected < 0 ? 0 : chip.selected / panorama_hud::kListSlots;
			RenderPanorama(slot);
			break;
		}
		case panorama_hud::Click::ListRow:
		{
			if (pm.editItem == kEditTabs)
			{
				PickTab(slot, pm.editPage * panorama_hud::kListSlots + index);
				break;
			}
			if (const int chip = -2 - pm.editItem; chip >= 0)
			{
				const MenuDef &shown = *Find(pm.handle);
				const int option = pm.editPage * panorama_hud::kListSlots + index;
				if (chip < static_cast<int>(shown.chips.size()) && index >= 0 && option < static_cast<int>(shown.chips[chip].options.size()))
				{
					// Like a dropdown, and a filter's selected option again clears it.
					const int selected = !shown.chips[chip].action && option == shown.chips[chip].selected ? -1 : option;
					pm.editItem = -1;
					pm.editPage = 0;
					PickChip(slot, chip, selected);
				}
				break;
			}
			const MenuItem *edited = EditedItem(slot);
			const int option = pm.editPage * panorama_hud::kListSlots + index;
			if (edited && edited->type == MenuItemType::Choice && index >= 0 && option < static_cast<int>(edited->options.size()))
			{
				// A pick closes the list, like a dropdown.
				const int itemIndex = pm.editItem;
				pm.editItem = -1;
				pm.editPage = 0;
				ChangeValue(slot, itemIndex, option);
			}
			break;
		}
		case panorama_hud::Click::ListPrev:
		case panorama_hud::Click::ListNext:
			if (EditedItem(slot) || pm.editItem <= -2)
			{
				// RenderPanorama clamps the page.
				pm.editPage = (std::max)(0, pm.editPage + (click == panorama_hud::Click::ListNext ? 1 : -1));
				RenderPanorama(slot);
			}
			break;
		case panorama_hud::Click::Nav:
		{
			if (index < 0 || index >= static_cast<int>(pm.panoramaNav.size()))
			{
				break;
			}
			const int target = pm.panoramaNav[index];
			if (PanoramaLayout(*Find(pm.handle)) == panorama_hud::Layout::List)
			{
				pm.page = target;
				RenderPanorama(slot);
			}
			else if (target == kNavMore)
			{
				// A second click closes the list.
				pm.editItem = pm.editItem == kEditTabs ? -1 : kEditTabs;
				pm.editPage = 0;
				RenderPanorama(slot);
			}
			else
			{
				PickTab(slot, target);
			}
			break;
		}
		case panorama_hud::Click::Item:
		{
			const MenuDef &shown = *Find(pm.handle);
			const int item = ClickedItem(slot, index);
			const int option = index >= 0 && index < static_cast<int>(pm.panoramaOptions.size()) ? pm.panoramaOptions[index] : -1;
			if (item >= 0 && option >= 0)
			{
				// A segment of a Choice drawn in place: the pick is the click.
				pm.editItem = -1;
				if (shown.items[item].disabled)
				{
					Render(slot);
				}
				else
				{
					ChangeValue(slot, item, option);
				}
			}
			else if (item >= 0)
			{
				// Select re-renders on a disabled row.
				Select(slot, item);
			}
			break;
		}
		case panorama_hud::Click::ItemDec:
		case panorama_hud::Click::ItemInc:
		{
			// A list row's own buttons: a Stepper a step, a Choice the option before or after, without the popup.
			const MenuDef &shown = *Find(pm.handle);
			const int item = ClickedItem(slot, index);
			if (item >= 0 && shown.items[item].type != MenuItemType::Toggle)
			{
				pm.editItem = -1;
				StepValue(slot, item, click == panorama_hud::Click::ItemDec ? -1 : 1);
			}
			break;
		}
		case panorama_hud::Click::Corner:
		{
			const MenuHandle handle = pm.handle;
			const int item = ClickedItem(slot, index);
			MenuItemCornerFn onCorner = Find(handle)->onCorner;
			DepthGuard guard(m_callbackDepth);
			if (item >= 0 && onCorner && guard.enter())
			{
				onCorner(handle, slot, item);
			}
			break;
		}
		case panorama_hud::Click::Action:
		{
			const int pinned = PinnedItem(*Find(pm.handle), pm.type);
			if (pinned >= 0)
			{
				Select(slot, pinned);
			}
			break;
		}
		case panorama_hud::Click::Action2:
		{
			const int secondary = SecondaryItem(*Find(pm.handle), pm.type);
			if (secondary >= 0)
			{
				Select(slot, secondary);
			}
			break;
		}
		case panorama_hud::Click::Control:
		{
			// The control behind the slot, as last drawn. A segment of a Choice drawn in place picks its option.
			if (index < 0 || index >= static_cast<int>(pm.controlSlots.size()))
			{
				break;
			}
			const int item = pm.controlSlots[index];
			const int option = pm.controlOptions[index];
			if (option < 0)
			{
				Select(slot, item);
				break;
			}
			pm.editItem = -1;
			if (const MenuItem *control = FindItem(pm.handle, item); control && !control->disabled)
			{
				ChangeValue(slot, item, option);
			}
			break;
		}
		case panorama_hud::Click::Help:
			pm.helpOpen = !pm.helpOpen;
			RenderPanorama(slot);
			break;
		case panorama_hud::Click::ControlTab:
		{
			std::vector<std::string> tabs;
			StudioControls(*Find(pm.handle), pm.controlTab, tabs);
			if (index < static_cast<int>(tabs.size()))
			{
				pm.controlTab = tabs[index];
				RenderPanorama(slot);
			}
			break;
		}
		case panorama_hud::Click::Stage:
			// The cursor goes, so the mouse turns the view. PollButtons brings it back on the next attack press.
			pm.editItem = -1;
			pm.turning = true;
			pm.buttonsPrimed = false;
			RenderPanorama(slot);
			break;
		case panorama_hud::Click::PagePrev:
		case panorama_hud::Click::PageNext:
		{
			// Within the current section, the tabs switch sections.
			const std::vector<Page> pages = Pages(*Find(pm.handle), pm.type);
			const int target = pm.page + (click == panorama_hud::Click::PageNext ? 1 : -1);
			if (pm.page < static_cast<int>(pages.size()) && target >= 0 && target < static_cast<int>(pages.size())
				&& pages[target].section == pages[pm.page].section)
			{
				pm.page = target;
				RenderPanorama(slot);
			}
			break;
		}
		case panorama_hud::Click::None:
			break;
	}
}
