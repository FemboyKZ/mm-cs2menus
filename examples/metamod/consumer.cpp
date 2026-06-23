// Test consumer for the CS2Menus C++ interface (ICS2Menus002).
//
// Acquires the interface via Metamod's factory, then opens a demo menu when a player types "!cs2menu" in chat.
// Demonstrates the acquire/re-resolve dance, submenus, select/end callbacks,
// and the SetExternalBusy coordination hook.
//
// Build: wired in as an extra target by examples/mm_consumer/AMBuilder,
// reusing the cs2menus SDK setup. Not shipped in releases (PackageScript ignores it).

#include <ISmmPlugin.h>
#include <sh_vector.h>

#include <eiface.h>
#include <icvar.h>
#include <iserver.h>

#include <cstring>

#include "public/ics2menus.h" // resolved via the src include dir (see AMBuilder)

#define CONSUMER_MAXPLAYERS 64

PLUGIN_GLOBALVARS();

SH_DECL_HOOK3_void(ICvar, DispatchConCommand, SH_NOATTRIB, 0, ConCommandRef, const CCommandContext &, const CCommand &);

static ICvar *g_pICvar = nullptr;
static ICS2Menus *g_pMenus = nullptr;

static MenuHandle g_DemoMenu = kInvalidMenuHandle;
static MenuHandle g_SubMenu = kInvalidMenuHandle;

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
		return "1.0.0";
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

void ConsumerPlugin::DropMenus()
{
	if (g_pMenus)
	{
		if (g_DemoMenu != kInvalidMenuHandle)
		{
			g_pMenus->DestroyMenu(g_DemoMenu);
		}
		if (g_SubMenu != kInvalidMenuHandle)
		{
			g_pMenus->DestroyMenu(g_SubMenu);
		}
	}
	g_DemoMenu = kInvalidMenuHandle;
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
