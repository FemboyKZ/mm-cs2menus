using Microsoft.Extensions.Logging;
using Cs2Menus;
using SwiftlyS2.Shared;
using SwiftlyS2.Shared.Commands;
using SwiftlyS2.Shared.Plugins;

namespace Cs2MenusSwiftlyExample;

// Test consumer for the cs2menus SwiftlyS2 bridge.
//   css_cs2menu       open the demo menu
//   css_cs2menu_busy  toggle external-busy for the caller's slot (coordination test)
[PluginMetadata(Id = "cs2menus_swiftly_example", Version = "1.0.0",
	Name = "CS2Menus Swiftly Example", Author = "jvnipers",
	Description = "Test consumer for the CS2Menus SwiftlyS2 bridge")]
public partial class Plugin : BasePlugin
{
	private Cs2MenusBridge _menus = null!;
	private Cs2Menu _menu = null!;

	public Plugin(ISwiftlyCore core) : base(core) { }

	public override void Load(bool hotReload)
	{
		if (!Cs2MenusBridge.LoadDefault(Core))
		{
			Core.Logger.LogError("CS2Menus not found. Install the Metamod plugin first.");
			return;
		}

		_menus = new Cs2MenusBridge();

		// Auto-yield to Swiftly's own menus: if a player opens one, cs2menus stands down.
		Cs2MenusBridge.TrackHostMenus(Core);

		// Build once, reuse. The bridge destroys it on Dispose (Unload).
		var sub = _menus.CreateMenu(MenuType.Default, "Submenu");
		sub.AddItem("Sub A", "sub_a").AddItem("Sub B", "sub_b");

		_menu = _menus.CreateMenu(MenuType.Default, "\x04CS2Menus demo",
			(m, slot, item) => Core.Logger.LogInformation("slot {Slot} picked '{Info}'", slot, m.GetItemInfo(item)));
		_menu.AddItem("Say hello", "hello")
			 .AddItem("Disabled row", "disabled", disabled: true)
			 .AddSubMenu("Open submenu", sub);
		_menu.Ended += (m, slot, reason) => Core.Logger.LogInformation("slot {Slot} menu ended: {Reason}", slot, reason);

		Core.Command.RegisterCommand("css_cs2menu", OnMenu);
		Core.Command.RegisterCommand("css_cs2menu_busy", OnBusy);
	}

	private void OnMenu(ICommandContext ctx)
	{
		if (ctx.Sender is not { } player)
		{
			ctx.Reply("Players only.");
			return;
		}
		if (!_menus.Available)
		{
			ctx.Reply("cs2menus unavailable.");
			return;
		}
		_menu.Display(player.Slot, 30f);
	}

	private void OnBusy(ICommandContext ctx)
	{
		if (ctx.Sender is not { } player)
		{
			return;
		}
		bool busy = !Cs2MenusBridge.IsHostMenuBusy(player.Slot);
		Cs2MenusBridge.SetHostMenuBusy(player.Slot, busy);
		ctx.Reply($"CS2Menus external-busy for your slot: {busy}");
	}

	public override void Unload()
	{
		_menus?.Dispose(); // destroys every menu, frees callback tokens
	}
}
