using System;
using System.Collections.Concurrent;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

namespace Cs2Menus;

public enum MenuType { Default = -1, Chat = 0, Html = 1 }

public enum MenuEndReason { Selected = 0, Exit = 1, Timeout = 2, Disconnect = 3, Cancelled = 4, Destroyed = 5 }

public enum MenuButton
{
	Default = 0, W, A, S, D, Use, Speed, Duck, Jump, Reload, Attack, Attack2, Score, Inspect, None
}

public enum MenuNavAction { Up = 0, Down = 1, Select = 2, Back = 3 }

/// <summary>
/// Entry point: load cs2menus and create menus. Framework-agnostic core:
/// each host package adds a <c>LoadDefault</c> partial that resolves the binary path.
///
/// cs2menus holds the native callback pointers for a menu until you destroy it.
/// If your plugin assembly unloads (hot-reload) while a menu still exists,
/// cs2menus will call freed pointers and crash.
/// Create one <see cref="Cs2MenusBridge"/> per plugin and <see cref="Dispose"/>
/// it in your plugin's unload path - that destroys every menu it created.
/// </summary>
public sealed partial class Cs2MenusBridge : IDisposable
{
	private readonly ConcurrentDictionary<uint, Cs2Menu> _menus = new();
	private bool _disposed;

	/// <summary>True if cs2menus has been loaded and its API is reachable.</summary>
	public bool Available => Cs2MenusNative.Loaded && Cs2MenusNative.Available() != 0;

	/// <summary>
	/// Resolve the cs2menus binary at an absolute path. Safe to call repeatedly, loads once.
	/// Prefer the host-specific <c>LoadDefault</c> overload.
	/// </summary>
	public static bool Load(string binaryPath) => Cs2MenusNative.Load(binaryPath);

	/// <summary>
	/// Tell cs2menus a host menu system owns this slot (busy=true) or released it (busy=false).
	/// While busy, cs2menus cancels any menu on the slot and refuses new ones,
	/// so it won't fight the host for chat input or the HTML channel.
	/// cs2menus never auto-reopens on release. No-op if cs2menus isn't loaded.
	/// Prefer the host-specific <c>TrackHostMenus</c> overload, which wires this automatically.
	/// </summary>
	public static void SetHostMenuBusy(int slot, bool busy)
	{
		if (Cs2MenusNative.Loaded)
		{
			Cs2MenusNative.SetExternalBusy(slot, busy);
		}
	}

	/// <summary>True if a host menu has been marked busy for this slot.</summary>
	public static bool IsHostMenuBusy(int slot) => Cs2MenusNative.Loaded && Cs2MenusNative.GetExternalBusy(slot);

	/// <summary>
	/// Render type of the cs2menus menu this slot has open (Chat if none).
	/// Lets a host check whether cs2menus is using the center-HTML channel before opening its own.
	/// Returns Chat if cs2menus isn't loaded.
	/// </summary>
	public static MenuType GetActiveType(int slot) => Cs2MenusNative.Loaded ? (MenuType)Cs2MenusNative.GetActiveType(slot) : MenuType.Chat;

	/// <summary>True if this slot currently has a cs2menus menu open.</summary>
	public static bool HasMenu(int slot) => Cs2MenusNative.Loaded && Cs2MenusNative.HasMenu(slot);

	/// <summary>Create a menu. <paramref name="onSelect"/> fires when a player picks an item.</summary>
	public Cs2Menu CreateMenu(MenuType type, string title, Action<Cs2Menu, int, int>? onSelect = null)
	{
		ObjectDisposedException.ThrowIf(_disposed, this);
		if (!Available)
		{
			throw new InvalidOperationException("cs2menus is not loaded. Call Load / LoadDefault first.");
		}

		var menu = new Cs2Menu(this, onSelect);
		unsafe
		{
			uint handle = Cs2MenusNative.Create((int)type, title, (nint)(delegate* unmanaged[Cdecl]<uint, int, int, void*, void>)&Trampolines.OnSelect, menu.Token);
			if (handle == 0)
			{
				menu.FreeToken();
				throw new InvalidOperationException("cs2menus CreateMenu failed.");
			}
			menu.Bind(handle);
			Cs2MenusNative.SetEndCallback(handle, (nint)(delegate* unmanaged[Cdecl]<uint, int, int, void*, void>)&Trampolines.OnEnd, menu.Token);
		}

		_menus[menu.Handle] = menu;
		return menu;
	}

	internal void Forget(uint handle) => _menus.TryRemove(handle, out _);

	public void Dispose()
	{
		if (_disposed)
		{
			return;
		}
		_disposed = true;
		foreach (var menu in _menus.Values) // snapshot: Dispose mutates the dictionary
		{
			menu.Dispose();
		}
		_menus.Clear();
	}
}

/// <summary>
/// A single menu. Build it, then <see cref="Display"/> to a player.
/// Dispose to free it (also done by the owning bridge on Dispose).
/// </summary>
public sealed class Cs2Menu : IDisposable
{
	private readonly Cs2MenusBridge _owner;
	private GCHandle _self;
	private bool _disposed;

	internal Action<Cs2Menu, int, int>? OnSelectHandler { get; }
	public event Action<Cs2Menu, int, MenuEndReason>? Ended;

	public uint Handle { get; private set; }
	internal unsafe void* Token => (void*)GCHandle.ToIntPtr(_self);

	internal Cs2Menu(Cs2MenusBridge owner, Action<Cs2Menu, int, int>? onSelect)
	{
		_owner = owner;
		OnSelectHandler = onSelect;
		_self = GCHandle.Alloc(this); // reference handed to native as the user token
	}

	internal void Bind(uint handle) => Handle = handle;
	internal void FreeToken()
	{
		if (_self.IsAllocated)
		{
			_self.Free();
		}
	}

	internal void RaiseEnded(int slot, MenuEndReason reason) => Ended?.Invoke(this, slot, reason);

	// --- Build (chainable) ---
	public Cs2Menu AddItem(string text, string info = "", bool disabled = false)
	{
		Cs2MenusNative.AddItem(Handle, text, info, disabled);
		return this;
	}

	public Cs2Menu AddSubMenu(string text, Cs2Menu child, string info = "")
	{
		Cs2MenusNative.AddSubmenu(Handle, text, child.Handle, info);
		return this;
	}

	public Cs2Menu SetTitle(string title) { Cs2MenusNative.SetTitle(Handle, title); return this; }
	public Cs2Menu SetExitButton(bool enabled) { Cs2MenusNative.SetExitButton(Handle, enabled); return this; }
	public Cs2Menu SetCloseOnSelect(bool enabled) { Cs2MenusNative.SetCloseOnSelect(Handle, enabled); return this; }
	public Cs2Menu SetExitItem(bool enabled) { Cs2MenusNative.SetExitItem(Handle, enabled); return this; }
	public Cs2Menu SetMenuKey(MenuNavAction action, MenuButton button) { Cs2MenusNative.SetMenuKey(Handle, (int)action, (int)button); return this; }
	public Cs2Menu SetStartItem(int item) { Cs2MenusNative.SetStartItem(Handle, item); return this; }

	// --- Show ---
	public bool Display(int slot, float duration = 0f) => Cs2MenusNative.Display(Handle, slot, duration);
	public void DisplayToAll(float duration = 0f) => Cs2MenusNative.DisplayToAll(Handle, duration);

	// --- Live mutation / introspection ---
	public int ItemCount => Cs2MenusNative.ItemCount(Handle);
	public string GetItemText(int item) => Cs2MenusNative.GetItemText(Handle, item);
	public string GetItemInfo(int item) => Cs2MenusNative.GetItemInfo(Handle, item);
	public void SetItemText(int item, string text) => Cs2MenusNative.SetItemText(Handle, item, text);
	public void SetItemInfo(int item, string info) => Cs2MenusNative.SetItemInfo(Handle, item, info);
	public void SetItemDisabled(int item, bool disabled) => Cs2MenusNative.SetItemDisabled(Handle, item, disabled);
	public bool GetItemDisabled(int item) => Cs2MenusNative.GetItemDisabled(Handle, item);
	public void RemoveItem(int item) => Cs2MenusNative.RemoveItem(Handle, item);
	public void RemoveAllItems() => Cs2MenusNative.RemoveAllItems(Handle);
	public int StartItem { get => Cs2MenusNative.GetStartItem(Handle); set => Cs2MenusNative.SetStartItem(Handle, value); }

	public void Dispose()
	{
		if (_disposed)
		{
			return;
		}
		_disposed = true;
		if (Handle != 0)
		{
			Cs2MenusNative.Destroy(Handle);
			_owner.Forget(Handle);
			Handle = 0;
		}
		FreeToken();
	}
}

/// <summary>
/// Native -> managed entry points. Run on the game main thread (cs2menus guarantees it).
/// Recover the <see cref="Cs2Menu"/> from the user token
/// and never let an exception escape back into native code.
/// </summary>
internal static unsafe class Trampolines
{
	[UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
	public static void OnSelect(uint menu, int slot, int item, void* user)
	{
		var m = Resolve(user);
		if (m is null)
		{
			return;
		}
		try
		{
			m.OnSelectHandler?.Invoke(m, slot, item);
		}
		catch
		{
			// swallow: throwing across the native boundary tears down the process
		}
	}

	[UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
	public static void OnEnd(uint menu, int slot, int reason, void* user)
	{
		var m = Resolve(user);
		if (m is null)
		{
			return;
		}
		try
		{
			m.RaiseEnded(slot, (MenuEndReason)reason);
		}
		catch
		{
		}
	}

	private static Cs2Menu? Resolve(void* user)
	{
		if (user == null)
		{
			return null;
		}
		var gch = GCHandle.FromIntPtr((nint)user);
		return gch.IsAllocated ? gch.Target as Cs2Menu : null;
	}
}
