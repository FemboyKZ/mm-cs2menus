using System;
using System.IO;
using System.Runtime.InteropServices;
using SwiftlyS2.Shared;

namespace Cs2Menus;

public sealed partial class Cs2MenusBridge
{
	// cs2menus ships at <csgo>/addons/cs2menus/bin/<platform>/cs2menus.<ext>.
	// Mirror its win64 / linuxsteamrt64 / osx64 layout.
	private const string PluginName = "cs2menus";

	/// <summary>
	/// Resolve and load the cs2menus binary from its standard addons location,
	/// using <see cref="ISwiftlyCore.CSGODirectory"/>. Call once in plugin load.
	/// Returns false if cs2menus isn't installed or the ABI mismatches.
	/// </summary>
	public static bool LoadDefault(ISwiftlyCore core)
	{
		ArgumentNullException.ThrowIfNull(core);
		return Load(ResolveBinaryPath(core.CSGODirectory));
	}

	/// <summary>The absolute path LoadDefault would use, for logging / diagnostics.</summary>
	public static string ResolveBinaryPath(string csgoDirectory)
	{
		(string platform, string ext) =
			RuntimeInformation.IsOSPlatform(OSPlatform.Windows) ? ("win64", ".dll") :
			RuntimeInformation.IsOSPlatform(OSPlatform.OSX) ? ("osx64", ".dylib") :
			("linuxsteamrt64", ".so");

		return Path.Combine(csgoDirectory, "addons", PluginName, "bin", platform, PluginName + ext);
	}

	/// <summary>
	/// Auto-yield cs2menus whenever a SwiftlyS2 menu opens for a player,
	/// and reclaim when it closes, by subscribing to the host menu manager's events.
	/// Call once in plugin load. So cs2menus never fights Swiftly's own menus for input.
	/// </summary>
	public static void TrackHostMenus(ISwiftlyCore core)
	{
		ArgumentNullException.ThrowIfNull(core);
		core.MenusAPI.MenuOpened += (_, e) =>
		{
			if (e.Player is { } p)
			{
				SetHostMenuBusy(p.Slot, true);
			}
		};
		core.MenusAPI.MenuClosed += (_, e) =>
		{
			if (e.Player is { } p)
			{
				SetHostMenuBusy(p.Slot, false);
			}
		};
	}
}
