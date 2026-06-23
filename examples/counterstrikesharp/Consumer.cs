using Microsoft.Extensions.Logging;
using CounterStrikeSharp.API.Core;
using CounterStrikeSharp.API.Modules.Commands;
using Cs2Menus;

namespace Cs2MenusCssExample;

// Test consumer for the cs2menus CounterStrikeSharp bridge.
//   css_cs2menu       open the demo menu
//   css_cs2menu_busy  toggle external-busy for the caller's slot (coordination test)
public class Plugin : BasePlugin
{
	public override string ModuleName => "CS2Menus CS# Example";
	public override string ModuleVersion => "1.0.0";
	public override string ModuleAuthor => "jvnipers";
	public override string ModuleDescription => "Test consumer for the CS2Menus CounterStrikeSharp bridge";

	private Cs2MenusBridge _menus = null!;
	private Cs2Menu _menu = null!;

	public override void Load(bool hotReload)
	{
		if (!Cs2MenusBridge.LoadDefault())
		{
			Logger.LogError("CS2Menus not found. Install the Metamod plugin first.");
			return;
		}

		_menus = new Cs2MenusBridge();

		// Build once, reuse. The bridge destroys it on Dispose (Unload).
		var sub = _menus.CreateMenu(MenuType.Default, "Submenu");
		sub.AddItem("Sub A", "sub_a").AddItem("Sub B", "sub_b");

		_menu = _menus.CreateMenu(MenuType.Default, "\x04CS2Menus demo",
			(m, slot, item) => Logger.LogInformation("slot {Slot} picked '{Info}'", slot, m.GetItemInfo(item)));
		_menu.AddItem("Say hello", "hello")
			 .AddItem("Disabled row", "disabled", disabled: true)
			 .AddSubMenu("Open submenu", sub);
		_menu.Ended += (m, slot, reason) => Logger.LogInformation("slot {Slot} menu ended: {Reason}", slot, reason);

		AddCommand("css_cs2menu", "Open the cs2menus demo", OnMenu);
		AddCommand("css_cs2menu_busy", "Toggle cs2menus external-busy for your slot", OnBusy);
	}

	private void OnMenu(CCSPlayerController? player, CommandInfo info)
	{
		if (player == null)
		{
			info.ReplyToCommand("Players only.");
			return;
		}
		if (!_menus.Available)
		{
			info.ReplyToCommand("cs2menus unavailable.");
			return;
		}

		// CS# has no global menu open/close events.
		// If you open a CS# menu yourself, 
		// also call Cs2MenusBridge.SetHostMenuBusy(player.Slot, true/false) around it so cs2menus yields.
		// (That also covers third-party CS# menu libraries.)
		_menu.Display(player.Slot, 30f);
	}

	private void OnBusy(CCSPlayerController? player, CommandInfo info)
	{
		if (player == null)
		{
			return;
		}
		bool busy = !Cs2MenusBridge.IsHostMenuBusy(player.Slot);
		Cs2MenusBridge.SetHostMenuBusy(player.Slot, busy);
		info.ReplyToCommand($"CS2Menus external-busy for your slot: {busy}");
	}

	public override void Unload(bool hotReload)
	{
		_menus?.Dispose(); // destroys every menu, frees callback tokens
	}
}
