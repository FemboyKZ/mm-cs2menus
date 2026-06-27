// Test consumer for the CS2Menus C++ interface (ICS2Menus003).
//
// Acquires the interface via Metamod's factory, then drives the API from chat.
// Demonstrates the acquire/re-resolve dance, submenus, select/end callbacks,
// and the SetExternalBusy coordination hook.
//
// Chat commands (typed by any player):
//   !cs2menu   - open the basic demo menu (submenu, select/end callbacks)
//   !cs2styled - open an HTML menu exercising the style/key/label/item API
//   !cs2all    - DisplayMenuToAll the demo menu to every connected player
//   !cs2cancel - CancelMenu on the caller's open menu
//   !cs2clear  - RemoveAllItems on a dedicated menu, repopulate it, then show it
//   !cs2theme  - stay-open menu, each row cycles item size/font/color on select
//   !cs2info   - dump the getter API for the caller's state to the console
//   !cs2busy   - toggle SetExternalBusy for the caller
//
// Between them the commands touch every ICS2Menus003 method, so this doubles
// as a smoke test for the interface surface.
//
// Build: wired in as an extra target by examples/mm_consumer/AMBuilder,
// reusing the cs2menus SDK setup. Not shipped in releases (PackageScript ignores it).

#include <ISmmPlugin.h>
#include <sh_vector.h>

#include <eiface.h>
#include <icvar.h>
#include <iserver.h>

#include <cstdio>
#include <cstring>

#include "public/ics2menus.h" // resolved via the src include dir (see AMBuilder)

#define CONSUMER_MAXPLAYERS 64

PLUGIN_GLOBALVARS();

SH_DECL_HOOK3_void(ICvar, DispatchConCommand, SH_NOATTRIB, 0, ConCommandRef, const CCommandContext &, const CCommand &);

static ICvar *g_pICvar = nullptr;
static ICS2Menus *g_pMenus = nullptr;

static MenuHandle g_DemoMenu = kInvalidMenuHandle;
static MenuHandle g_SubMenu = kInvalidMenuHandle;
static MenuHandle g_StyledMenu = kInvalidMenuHandle;
static MenuHandle g_ClearMenu = kInvalidMenuHandle;
static MenuHandle g_ThemeMenu = kInvalidMenuHandle;

// Style values the theme menu cycles through on select, so you can eyeball that each one renders.
static const char *const kSizeTokens[] = {"xs", "s", "sm", "m", "ml", "l", "xl", "xxl", "xxxl"};
static const char *const kFontFaces[] = {"", "stratum-bold", "stratum-light-italic", "mono-spaced-font-bold",
										 "stratum-black text-uppercase text-shadow-basic"};
static const char *const kItemColors[] = {"#FFFFFF", "#ff2ee7", "#00d8ff", "#ffcc00", "#39ff14"};
static constexpr int kSizeCount = static_cast<int>(sizeof(kSizeTokens) / sizeof(kSizeTokens[0]));
static constexpr int kFontCount = static_cast<int>(sizeof(kFontFaces) / sizeof(kFontFaces[0]));
static constexpr int kColorCount = static_cast<int>(sizeof(kItemColors) / sizeof(kItemColors[0]));
static int g_sizeIdx = 3; // "m"
static int g_fontIdx = 0;
static int g_colorIdx = 0;

class ConsumerPlugin : public ISmmPlugin, public IMetamodListener
{
public:
	bool Load(PluginId id, ISmmAPI *ismm, char *error, size_t maxlen, bool late) override;
	bool Unload(char *error, size_t maxlen) override;

	// IMetamodListener: re-resolve so we never hold a dangling cs2menus pointer.
	void OnPluginLoad(PluginId id) override;
	void OnPluginUnload(PluginId id) override;

	void Hook_DispatchConCommand(ConCommandRef cmd, const CCommandContext &ctx, const CCommand &args);

	void BuildMenu();
	void BuildStyledMenu();
	void BuildClearMenu();
	void BuildThemeMenu();
	void DumpInfo(int slot);
	void DropMenus();

	const char *GetAuthor() override
	{
		return "jvnipers";
	}

	const char *GetName() override
	{
		return "CS2Menus Consumer Example";
	}

	const char *GetDescription() override
	{
		return "Test consumer for the cs2menus C++ interface";
	}

	const char *GetURL() override
	{
		return "https://github.com/FemboyKZ/mm-cs2menus";
	}

	const char *GetLicense() override
	{
		return "GPLv3";
	}

	const char *GetVersion() override
	{
		return "1.0.1";
	}

	const char *GetDate() override
	{
		return __DATE__;
	}

	const char *GetLogTag() override
	{
		return "MENU-TEST";
	}
};

ConsumerPlugin g_ThisPlugin;
PLUGIN_EXPOSE(ConsumerPlugin, g_ThisPlugin);

bool ConsumerPlugin::Load(PluginId id, ISmmAPI *ismm, char *error, size_t maxlen, bool late)
{
	PLUGIN_SAVEVARS();

	GET_V_IFACE_CURRENT(GetEngineFactory, g_pICvar, ICvar, CVAR_INTERFACE_VERSION);

	g_SMAPI->AddListener(this, this);
	SH_ADD_HOOK(ICvar, DispatchConCommand, g_pICvar, SH_MEMBER(this, &ConsumerPlugin::Hook_DispatchConCommand), false);

	g_pMenus = (ICS2Menus *)g_SMAPI->MetaFactory(CS2MENUS_INTERFACE, nullptr, nullptr);
	if (!g_pMenus)
	{
		META_CONPRINTF("[CS2Menus-test] cs2menus not loaded yet, will resolve on plugin load.\n");
	}

	META_CONPRINTF("[CS2Menus-test] Loaded. Type !cs2menu in chat.\n");
	return true;
}

bool ConsumerPlugin::Unload(char * /*error*/, size_t /*maxlen*/)
{
	SH_REMOVE_HOOK(ICvar, DispatchConCommand, g_pICvar, SH_MEMBER(this, &ConsumerPlugin::Hook_DispatchConCommand), false);

	// Destroy everything we created so cs2menus never calls our (soon-gone) lambdas.
	DropMenus();
	return true;
}

void ConsumerPlugin::OnPluginLoad(PluginId /*id*/)
{
	// cs2menus may have just loaded. Resolve and (re)build lazily on next use.
	if (!g_pMenus)
	{
		g_pMenus = (ICS2Menus *)g_SMAPI->MetaFactory(CS2MENUS_INTERFACE, nullptr, nullptr);
		g_DemoMenu = kInvalidMenuHandle;
		g_SubMenu = kInvalidMenuHandle;
		g_StyledMenu = kInvalidMenuHandle;
		g_ClearMenu = kInvalidMenuHandle;
		g_ThemeMenu = kInvalidMenuHandle;
	}
}

void ConsumerPlugin::OnPluginUnload(PluginId /*id*/)
{
	// If cs2menus went away, drop our now-invalid pointer and handles.
	ICS2Menus *current = (ICS2Menus *)g_SMAPI->MetaFactory(CS2MENUS_INTERFACE, nullptr, nullptr);
	if (!current)
	{
		g_pMenus = nullptr;
		g_DemoMenu = kInvalidMenuHandle;
		g_SubMenu = kInvalidMenuHandle;
		g_StyledMenu = kInvalidMenuHandle;
		g_ClearMenu = kInvalidMenuHandle;
		g_ThemeMenu = kInvalidMenuHandle;
	}
}

void ConsumerPlugin::BuildMenu()
{
	if (!g_pMenus || g_DemoMenu != kInvalidMenuHandle)
	{
		return;
	}

	g_SubMenu = g_pMenus->CreateMenu(MenuType::Default, "Submenu", nullptr);
	g_pMenus->AddItem(g_SubMenu, "Sub A", "sub_a", false);
	g_pMenus->AddItem(g_SubMenu, "Sub B", "sub_b", false);

	g_DemoMenu = g_pMenus->CreateMenu(MenuType::Default, "\x04CS2Menus native demo", [](MenuHandle menu, int slot, int item)
									  { META_CONPRINTF("[CS2Menus-test] slot %d picked '%s'\n", slot, g_pMenus->GetItemInfo(menu, item)); });
	g_pMenus->AddItem(g_DemoMenu, "Say hello", "hello", false);
	g_pMenus->AddItem(g_DemoMenu, "Disabled row", "disabled", true);
	g_pMenus->AddSubMenu(g_DemoMenu, "Open submenu", g_SubMenu, "");
	g_pMenus->SetMenuEndCallback(g_DemoMenu, [](MenuHandle /*menu*/, int slot, MenuEndReason reason)
								 { META_CONPRINTF("[CS2Menus-test] slot %d menu ended, reason %d\n", slot, static_cast<int>(reason)); });
}

// Build an HTML menu that exercises the item, style, key and label API.
// Forced to HTML so the style overrides actually render regardless of the viewer's saved preference.
void ConsumerPlugin::BuildStyledMenu()
{
	if (!g_pMenus || g_StyledMenu != kInvalidMenuHandle)
	{
		return;
	}

	// The submenu is shared with the basic demo, build it on demand.
	if (g_SubMenu == kInvalidMenuHandle)
	{
		g_SubMenu = g_pMenus->CreateMenu(MenuType::Default, "Submenu", nullptr);
		g_pMenus->AddItem(g_SubMenu, "Sub A", "sub_a", false);
		g_pMenus->AddItem(g_SubMenu, "Sub B", "sub_b", false);
	}

	g_StyledMenu = g_pMenus->CreateMenu(MenuType::Html, "placeholder title",
										[](MenuHandle menu, int slot, int item)
										{
											META_CONPRINTF("[CS2Menus-test] slot %d styled pick item %d text '%s' info '%s'\n", slot, item,
														   g_pMenus->GetItemText(menu, item), g_pMenus->GetItemInfo(menu, item));
										});

	g_pMenus->SetTitle(g_StyledMenu, "\x04Styled HTML demo");
	g_pMenus->SetMenuForceType(g_StyledMenu, true); // lock to HTML so styles always apply
	g_pMenus->SetExitButton(g_StyledMenu, true);
	g_pMenus->SetExitItem(g_StyledMenu, true);       // selectable Exit row at the end
	g_pMenus->SetCloseOnSelect(g_StyledMenu, false); // stay open, re-render after each pick
	g_pMenus->SetStartItem(g_StyledMenu, 1);         // open with the cursor on the second row

	// Raw item: text passed straight through as Panorama markup.
	int rawIdx = g_pMenus->AddItem(g_StyledMenu, "<font color='#ffcc00'>raw markup row</font>", "raw", false);
	g_pMenus->SetItemRaw(g_StyledMenu, rawIdx, true);

	// Icon item: escaped text with a leading image.
	// Panorama decodes png/jpg/svg, not .ico, and cs2menus emits a bare <img> with no
	// width/height, so the source must already be small (it renders at native size).
	int iconIdx = g_pMenus->AddItem(g_StyledMenu, "icon row", "icon", false);
	g_pMenus->SetItemIcon(g_StyledMenu, iconIdx, "https://www.google.com/s2/favicons?sz=32&domain=steampowered.com");

	// Disabled item via the setter rather than the AddItem flag.
	int offIdx = g_pMenus->AddItem(g_StyledMenu, "disabled row", "off", false);
	g_pMenus->SetItemDisabled(g_StyledMenu, offIdx, true);

	// Insert pushes the above rows down by one.
	g_pMenus->InsertItem(g_StyledMenu, 0, "inserted first", "first", false);

	// Add then remove a throwaway to cover InsertItem's sibling.
	int tmpIdx = g_pMenus->AddItem(g_StyledMenu, "temp", "temp", false);
	g_pMenus->RemoveItem(g_StyledMenu, tmpIdx);

	// Attach the shared submenu through SetItemSubmenu instead of AddSubMenu.
	int subIdx = g_pMenus->AddItem(g_StyledMenu, "open submenu", "sub", false);
	g_pMenus->SetItemSubmenu(g_StyledMenu, subIdx, g_SubMenu);

	// Rewrite an existing item's text/info after the fact.
	g_pMenus->SetItemText(g_StyledMenu, iconIdx, "icon row (renamed)");
	g_pMenus->SetItemInfo(g_StyledMenu, iconIdx, "icon2");

	// Per-menu style overrides.
	g_pMenus->SetMenuStyle(g_StyledMenu, MenuStyle::Align, "center");
	g_pMenus->SetMenuStyle(g_StyledMenu, MenuStyle::FontFace, "stratum-bold text-shadow-basic");
	g_pMenus->SetMenuStyle(g_StyledMenu, MenuStyle::TitleColor, "#00d8ff");
	g_pMenus->SetMenuStyle(g_StyledMenu, MenuStyle::VisibleItems, "6");
	g_pMenus->SetMenuStyle(g_StyledMenu, MenuStyle::Marker, ">> ");

	// Per-menu navigation key overrides. W/S still move the cursor, Select moves off the
	// default D onto E (Use). The footer key hints update to match.
	// To test disabling an action use MenuButton::None, e.g. Up=None makes Down alone
	// cycle the cursor with wraparound. Don't disable every move key or the menu can't navigate.
	g_pMenus->SetMenuKey(g_StyledMenu, MenuNavAction::Up, MenuButton::W);
	g_pMenus->SetMenuKey(g_StyledMenu, MenuNavAction::Down, MenuButton::S);
	g_pMenus->SetMenuKey(g_StyledMenu, MenuNavAction::Select, MenuButton::Use);

	// Rename the Exit label for this menu only.
	g_pMenus->SetMenuLabel(g_StyledMenu, MenuLabel::Exit, "Close");

	g_pMenus->SetMenuEndCallback(g_StyledMenu, [](MenuHandle /*menu*/, int slot, MenuEndReason reason)
								 { META_CONPRINTF("[CS2Menus-test] slot %d styled menu ended, reason %d\n", slot, static_cast<int>(reason)); });
}

// Dump the read-side API for a slot to the console. Covers the getters the build paths don't print.
void ConsumerPlugin::DumpInfo(int slot)
{
	if (!g_pMenus)
	{
		return;
	}

	META_CONPRINTF("[CS2Menus-test] --- info for slot %d ---\n", slot);
	META_CONPRINTF("  HasMenu=%d activeMenu=%u activeType=%d selectedItem=%d externalBusy=%d\n", g_pMenus->HasMenu(slot) ? 1 : 0,
				   g_pMenus->GetActiveMenu(slot), static_cast<int>(g_pMenus->GetActiveMenuType(slot)), g_pMenus->GetSelectedItem(slot),
				   g_pMenus->GetExternalBusy(slot) ? 1 : 0);

	// Inspect the menu the player has open right now, else fall back to a built handle.
	// A menu definition lives until DestroyMenu, so the property lines below report the
	// last-built state of that handle even when nothing is displayed (by design, not a bug).
	MenuHandle menu = g_pMenus->HasMenu(slot) ? g_pMenus->GetActiveMenu(slot) : g_StyledMenu != kInvalidMenuHandle ? g_StyledMenu : g_DemoMenu;
	META_CONPRINTF("  menu %u valid=%d type=%d title='%s' items=%d\n", menu, g_pMenus->IsValidMenu(menu) ? 1 : 0,
				   static_cast<int>(g_pMenus->GetMenuType(menu)), g_pMenus->GetTitle(menu), g_pMenus->GetItemCount(menu));
	META_CONPRINTF("  exitBtn=%d closeOnSelect=%d exitItem=%d forceType=%d startItem=%d\n", g_pMenus->GetExitButton(menu) ? 1 : 0,
				   g_pMenus->GetCloseOnSelect(menu) ? 1 : 0, g_pMenus->GetExitItem(menu) ? 1 : 0, g_pMenus->GetMenuForceType(menu) ? 1 : 0,
				   g_pMenus->GetStartItem(menu));
	META_CONPRINTF("  selectKey=%d exitLabel='%s' alignStyle='%s'\n", static_cast<int>(g_pMenus->GetMenuKey(menu, MenuNavAction::Select)),
				   g_pMenus->GetMenuLabel(menu, MenuLabel::Exit), g_pMenus->GetMenuStyle(menu, MenuStyle::Align));

	for (int i = 0; i < g_pMenus->GetItemCount(menu); ++i)
	{
		META_CONPRINTF("  item %d text='%s' info='%s' disabled=%d raw=%d icon='%s' submenu=%u\n", i, g_pMenus->GetItemText(menu, i),
					   g_pMenus->GetItemInfo(menu, i), g_pMenus->GetItemDisabled(menu, i) ? 1 : 0, g_pMenus->GetItemRaw(menu, i) ? 1 : 0,
					   g_pMenus->GetItemIcon(menu, i), g_pMenus->GetItemSubmenu(menu, i));
	}
}

// Rebuild a dedicated menu from scratch: clear every item, then add a fresh batch.
// Demonstrates RemoveAllItems and gives several rows to navigate.
// Kept separate from the demo menu so clearing never mutates it (re-show with !cs2menu stays canonical).
void ConsumerPlugin::BuildClearMenu()
{
	if (!g_pMenus)
	{
		return;
	}

	if (g_ClearMenu == kInvalidMenuHandle)
	{
		g_ClearMenu =
			g_pMenus->CreateMenu(MenuType::Default, "\x04Clear/rebuild demo", [](MenuHandle menu, int slot, int item)
								 { META_CONPRINTF("[CS2Menus-test] slot %d clear-menu picked '%s'\n", slot, g_pMenus->GetItemInfo(menu, item)); });
	}

	g_pMenus->RemoveAllItems(g_ClearMenu);
	for (int i = 1; i <= 6; ++i)
	{
		char text[32];
		char info[16];
		snprintf(text, sizeof(text), "Row %d", i);
		snprintf(info, sizeof(info), "row_%d", i);
		g_pMenus->AddItem(g_ClearMenu, text, info, false);
	}
}

// Push the current size/font/color into the theme menu and relabel its rows to match.
// SetMenuStyle/SetItemText re-render any viewer, so the change shows the instant a row is picked.
static void ApplyTheme()
{
	if (!g_pMenus || g_ThemeMenu == kInvalidMenuHandle)
	{
		return;
	}

	g_pMenus->SetMenuStyle(g_ThemeMenu, MenuStyle::ItemSize, kSizeTokens[g_sizeIdx]);
	g_pMenus->SetMenuStyle(g_ThemeMenu, MenuStyle::FontFace, kFontFaces[g_fontIdx]);
	g_pMenus->SetMenuStyle(g_ThemeMenu, MenuStyle::ItemColor, kItemColors[g_colorIdx]);

	char buf[96];
	snprintf(buf, sizeof(buf), "Item size: %s", kSizeTokens[g_sizeIdx]);
	g_pMenus->SetItemText(g_ThemeMenu, 0, buf);
	snprintf(buf, sizeof(buf), "Font: %s", kFontFaces[g_fontIdx][0] ? kFontFaces[g_fontIdx] : "default");
	g_pMenus->SetItemText(g_ThemeMenu, 1, buf);
	snprintf(buf, sizeof(buf), "Item color: %s", kItemColors[g_colorIdx]);
	g_pMenus->SetItemText(g_ThemeMenu, 2, buf);
}

// A stay-open HTML menu where each row cycles one style field (size / font / color) on select.
// The row text and the whole menu re-render with the new value so you can confirm it actually displays.
void ConsumerPlugin::BuildThemeMenu()
{
	if (!g_pMenus || g_ThemeMenu != kInvalidMenuHandle)
	{
		return;
	}

	g_ThemeMenu = g_pMenus->CreateMenu(MenuType::Html, "\x04Theme toggle demo",
									   [](MenuHandle menu, int /*slot*/, int item)
									   {
										   const char *info = g_pMenus->GetItemInfo(menu, item);
										   if (strcmp(info, "size") == 0)
										   {
											   g_sizeIdx = (g_sizeIdx + 1) % kSizeCount;
										   }
										   else if (strcmp(info, "font") == 0)
										   {
											   g_fontIdx = (g_fontIdx + 1) % kFontCount;
										   }
										   else if (strcmp(info, "color") == 0)
										   {
											   g_colorIdx = (g_colorIdx + 1) % kColorCount;
										   }
										   ApplyTheme();
									   });

	g_pMenus->SetMenuForceType(g_ThemeMenu, true);  // HTML so the style fields actually apply
	g_pMenus->SetCloseOnSelect(g_ThemeMenu, false); // stay open so you watch the value change
	// HighlightText off so the cursor row keeps ItemColor too, otherwise it shows NavColor.
	g_pMenus->SetMenuStyle(g_ThemeMenu, MenuStyle::HighlightText, "0");

	g_pMenus->AddItem(g_ThemeMenu, "Item size", "size", false);
	g_pMenus->AddItem(g_ThemeMenu, "Font", "font", false);
	g_pMenus->AddItem(g_ThemeMenu, "Item color", "color", false);

	ApplyTheme(); // seed the styles + row labels before first display
}

void ConsumerPlugin::DropMenus()
{
	if (g_pMenus)
	{
		if (g_DemoMenu != kInvalidMenuHandle)
		{
			g_pMenus->DestroyMenu(g_DemoMenu);
		}
		if (g_StyledMenu != kInvalidMenuHandle)
		{
			g_pMenus->DestroyMenu(g_StyledMenu);
		}
		if (g_ClearMenu != kInvalidMenuHandle)
		{
			g_pMenus->DestroyMenu(g_ClearMenu);
		}
		if (g_ThemeMenu != kInvalidMenuHandle)
		{
			g_pMenus->DestroyMenu(g_ThemeMenu);
		}
		if (g_SubMenu != kInvalidMenuHandle)
		{
			g_pMenus->DestroyMenu(g_SubMenu);
		}
	}
	g_DemoMenu = kInvalidMenuHandle;
	g_StyledMenu = kInvalidMenuHandle;
	g_ClearMenu = kInvalidMenuHandle;
	g_ThemeMenu = kInvalidMenuHandle;
	g_SubMenu = kInvalidMenuHandle;
}

void ConsumerPlugin::Hook_DispatchConCommand(ConCommandRef /*cmd*/, const CCommandContext &ctx, const CCommand &args)
{
	const char *cmdName = args.Arg(0);
	if (!cmdName || (strcmp(cmdName, "say") != 0 && strcmp(cmdName, "say_team") != 0))
	{
		RETURN_META(MRES_IGNORED);
	}

	int slot = ctx.GetPlayerSlot().Get();
	if (slot < 0 || slot > CONSUMER_MAXPLAYERS)
	{
		RETURN_META(MRES_IGNORED);
	}

	const char *msg = args.ArgS();
	if (!msg)
	{
		RETURN_META(MRES_IGNORED);
	}

	// args.ArgS() includes the quotes CS2 wraps around chat, so substring-match.
	if (strstr(msg, "!cs2menu"))
	{
		if (!g_pMenus)
		{
			META_CONPRINTF("[CS2Menus-test] cs2menus unavailable.\n");
			RETURN_META(MRES_SUPERCEDE);
		}
		BuildMenu();
		g_pMenus->DisplayMenu(g_DemoMenu, slot, 30.0f);
		RETURN_META(MRES_SUPERCEDE);
	}

	if (strstr(msg, "!cs2styled"))
	{
		if (!g_pMenus)
		{
			META_CONPRINTF("[CS2Menus-test] cs2menus unavailable.\n");
			RETURN_META(MRES_SUPERCEDE);
		}
		BuildStyledMenu();
		g_pMenus->DisplayMenu(g_StyledMenu, slot, 30.0f);
		RETURN_META(MRES_SUPERCEDE);
	}

	if (strstr(msg, "!cs2all"))
	{
		if (g_pMenus)
		{
			BuildMenu();
			g_pMenus->DisplayMenuToAll(g_DemoMenu, 30.0f);
			META_CONPRINTF("[CS2Menus-test] demo menu displayed to all.\n");
		}
		RETURN_META(MRES_SUPERCEDE);
	}

	if (strstr(msg, "!cs2cancel"))
	{
		if (g_pMenus)
		{
			g_pMenus->CancelMenu(slot);
			META_CONPRINTF("[CS2Menus-test] slot %d menu cancelled.\n", slot);
		}
		RETURN_META(MRES_SUPERCEDE);
	}

	if (strstr(msg, "!cs2clear"))
	{
		if (g_pMenus)
		{
			BuildClearMenu();
			g_pMenus->DisplayMenu(g_ClearMenu, slot, 30.0f);
			META_CONPRINTF("[CS2Menus-test] clear menu cleared and rebuilt.\n");
		}
		RETURN_META(MRES_SUPERCEDE);
	}

	if (strstr(msg, "!cs2theme"))
	{
		if (g_pMenus)
		{
			BuildThemeMenu();
			g_pMenus->DisplayMenu(g_ThemeMenu, slot, 0.0f); // no timeout, cycle styles at your own pace
		}
		RETURN_META(MRES_SUPERCEDE);
	}

	if (strstr(msg, "!cs2info"))
	{
		DumpInfo(slot);
		RETURN_META(MRES_SUPERCEDE);
	}

	if (strstr(msg, "!cs2busy"))
	{
		if (g_pMenus)
		{
			bool busy = !g_pMenus->GetExternalBusy(slot);
			g_pMenus->SetExternalBusy(slot, busy);
			META_CONPRINTF("[CS2Menus-test] slot %d external-busy: %d\n", slot, busy ? 1 : 0);
		}
		RETURN_META(MRES_SUPERCEDE);
	}

	RETURN_META(MRES_IGNORED);
}
