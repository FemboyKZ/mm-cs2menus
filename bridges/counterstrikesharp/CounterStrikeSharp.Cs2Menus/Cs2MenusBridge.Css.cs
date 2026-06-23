using System;
using System.IO;
using System.Runtime.InteropServices;
using CounterStrikeSharp.API;

namespace Cs2Menus;

public sealed partial class Cs2MenusBridge
{
	// cs2menus ships at <csgo>/addons/cs2menus/bin/<platform>/cs2menus.<ext>.
	// Mirror its win64 / linuxsteamrt64 / osx64 layout.
	private const string PluginName = "cs2menus";

	/// <summary>
	/// Resolve and load the cs2menus binary from its standard addons location,
	/// using <see cref="Server.GameDirectory"/> (the absolute game/csgo path).
	/// Call once in your plugin's Load. Returns false if cs2menus isn't installed or the ABI mismatches.
	/// </summary>
	public static bool LoadDefault()
	{
		return Load(ResolveBinaryPath(Server.GameDirectory));
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
}
