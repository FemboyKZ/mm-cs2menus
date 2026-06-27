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

public enum MenuLabel { Exit = 0, NextPage = 1, PrevPage = 2, Move = 3, Scroll = 4, Select = 5 }

/// <summary>
/// Per-menu HTML style fields settable via <see cref="Cs2Menu.SetMenuStyle"/>.
/// Each overrides the matching server default for one menu. Pass "" to inherit. HTML menus only.
/// Sizes take a token ("xs" "s" "sm" "m" "ml" "l" "xl" "xxl" "xxxl"), colors "#RRGGBB", toggles "1"/"0".
/// </summary>
public enum MenuStyle
{
	// Global / layout
	Align = 0,
	FontFace,
	VisibleItems,
	// Title
	TitleColor,
	TitleSize,
	RawTitle,
	// Items
	ItemColor,
	ItemSize,
	DisabledColor,
	SubmenuSuffix,
	// Cursor row
	NavColor,
	Marker,
	HighlightText,
	// Position counter
	ShowCounter,
	CounterColor,
	CounterSize,
	CounterFormat,
	// Key-hint footer
	ShowFooter,
	FooterColor,
	FooterSize,
	FooterSeparator,
	FooterHintFormat,
	FooterRangeFormat
}

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

	/// <summary>Close whatever cs2menus menu this slot has open (fires its end callback). Does not destroy the menu object. No-op if none / not loaded.</summary>
	public static void Cancel(int slot)
	{
		if (Cs2MenusNative.Loaded)
		{
			Cs2MenusNative.Cancel(slot);
		}
	}

	/// <summary>Abs index of the item the slot has highlighted in an HTML menu, or -1 (no menu / chat menu / Exit row / not loaded).</summary>
	public static int GetSelectedItem(int slot) => Cs2MenusNative.Loaded ? Cs2MenusNative.GetSelectedItem(slot) : -1;

	/// <summary>The menu this bridge created that the slot currently has open, or null (none / created by another bridge / not loaded).</summary>
	public Cs2Menu? GetActiveMenu(int slot) => Cs2MenusNative.Loaded ? Find(Cs2MenusNative.GetActiveMenu(slot)) : null;

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

	/// <summary>The tracked menu for a handle, or null if this bridge didn't create it.</summary>
	internal Cs2Menu? Find(uint handle) => _menus.TryGetValue(handle, out var m) ? m : null;

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

	// --- Menu properties (chainable setters + paired getters) ---
	public Cs2Menu SetTitle(string title) { Cs2MenusNative.SetTitle(Handle, title); return this; }
	/// <summary>The menu's title as last set (the key/literal, not translated text).</summary>
	public string Title => Cs2MenusNative.GetTitle(Handle);
	/// <summary>The menu's base render type as created. For the per-viewer type of an open display use <see cref="Cs2MenusBridge.GetActiveType"/>.</summary>
	public MenuType Type => (MenuType)Cs2MenusNative.GetMenuType(Handle);
	public Cs2Menu SetExitButton(bool enabled) { Cs2MenusNative.SetExitButton(Handle, enabled); return this; }
	/// <summary>Whether the trailing "0. Exit" entry is shown (see <see cref="SetExitButton"/>).</summary>
	public bool ExitButton => Cs2MenusNative.GetExitButton(Handle);
	public Cs2Menu SetCloseOnSelect(bool enabled) { Cs2MenusNative.SetCloseOnSelect(Handle, enabled); return this; }
	/// <summary>Whether the menu closes after a selection (see <see cref="SetCloseOnSelect"/>).</summary>
	public bool CloseOnSelect => Cs2MenusNative.GetCloseOnSelect(Handle);
	public Cs2Menu SetExitItem(bool enabled) { Cs2MenusNative.SetExitItem(Handle, enabled); return this; }
	/// <summary>Whether the HTML selectable Exit row is shown (see <see cref="SetExitItem"/>).</summary>
	public bool ExitItem => Cs2MenusNative.GetExitItem(Handle);
	/// <summary>
	/// Lock this menu's render type so the viewing player's saved preference can't change it.
	/// Use it when the menu depends on a specific type (e.g. HTML-only icons or raw markup).
	/// </summary>
	public Cs2Menu SetForceType(bool force = true) { Cs2MenusNative.SetForceType(Handle, force); return this; }
	/// <summary>Whether the render type is locked against the viewer's preference (see <see cref="SetForceType"/>).</summary>
	public bool ForceType => Cs2MenusNative.GetForceType(Handle);
	public Cs2Menu SetStartItem(int item) { Cs2MenusNative.SetStartItem(Handle, item); return this; }
	/// <summary>Item the menu opens on (HTML cursor / chat page). Clamped at display.</summary>
	public int StartItem { get => Cs2MenusNative.GetStartItem(Handle); set => Cs2MenusNative.SetStartItem(Handle, value); }
	public Cs2Menu SetMenuKey(MenuNavAction action, MenuButton button) { Cs2MenusNative.SetMenuKey(Handle, (int)action, (int)button); return this; }
	/// <summary>The per-menu nav-key override for an action (Default if unset, None if disabled). See <see cref="SetMenuKey"/>.</summary>
	public MenuButton GetMenuKey(MenuNavAction action) => (MenuButton)Cs2MenusNative.GetMenuKey(Handle, (int)action);
	/// <summary>Rename a built-in label (Exit, page nav, footer hints). "" restores the configured default.</summary>
	public Cs2Menu SetMenuLabel(MenuLabel label, string text) { Cs2MenusNative.SetMenuLabel(Handle, (int)label, text); return this; }
	/// <summary>The label's current key (last set, or the default). This is the key/literal, not the translated text.</summary>
	public string GetMenuLabel(MenuLabel label) => Cs2MenusNative.GetMenuLabel(Handle, (int)label);
	/// <summary>Override one HTML style field for this menu (see <see cref="MenuStyle"/>). "" inherits the server default. No-op for chat menus.</summary>
	public Cs2Menu SetMenuStyle(MenuStyle field, string value) { Cs2MenusNative.SetMenuStyle(Handle, (int)field, value); return this; }
	/// <summary>This menu's effective value for a style field (the override if set, else the server default).</summary>
	public string GetMenuStyle(MenuStyle field) => Cs2MenusNative.GetMenuStyle(Handle, (int)field);

	// --- Items ---
	public Cs2Menu AddItem(string text, string info = "", bool disabled = false)
	{
		Cs2MenusNative.AddItem(Handle, text, info, disabled);
		return this;
	}

	/// <summary>Insert an item at <paramref name="pos"/> (clamped to [0, ItemCount]); later items shift down. Returns the index, or -1.</summary>
	public int InsertItem(int pos, string text, string info = "", bool disabled = false) => Cs2MenusNative.InsertItem(Handle, pos, text, info, disabled);

	public Cs2Menu AddSubMenu(string text, Cs2Menu child, string info = "")
	{
		Cs2MenusNative.AddSubmenu(Handle, text, child.Handle, info);
		return this;
	}

	public void RemoveItem(int item) => Cs2MenusNative.RemoveItem(Handle, item);
	public void RemoveAllItems() => Cs2MenusNative.RemoveAllItems(Handle);
	public int ItemCount => Cs2MenusNative.ItemCount(Handle);
	public void SetItemText(int item, string text) => Cs2MenusNative.SetItemText(Handle, item, text);
	public string GetItemText(int item) => Cs2MenusNative.GetItemText(Handle, item);
	public void SetItemInfo(int item, string info) => Cs2MenusNative.SetItemInfo(Handle, item, info);
	public string GetItemInfo(int item) => Cs2MenusNative.GetItemInfo(Handle, item);
	public void SetItemDisabled(int item, bool disabled) => Cs2MenusNative.SetItemDisabled(Handle, item, disabled);
	public bool GetItemDisabled(int item) => Cs2MenusNative.GetItemDisabled(Handle, item);
	/// <summary>HTML menus: render the item's text as raw Panorama markup (unescaped). No-op for chat menus.</summary>
	public void SetItemRaw(int item, bool raw) => Cs2MenusNative.SetItemRaw(Handle, item, raw);
	public bool GetItemRaw(int item) => Cs2MenusNative.GetItemRaw(Handle, item);
	/// <summary>HTML menus: show an image before the item's text (icon URL / packaged path). "" removes it.</summary>
	public void SetItemIcon(int item, string url) => Cs2MenusNative.SetItemIcon(Handle, item, url);
	public string GetItemIcon(int item) => Cs2MenusNative.GetItemIcon(Handle, item);

	/// <summary>Attach <paramref name="child"/> as an existing item's submenu (null detaches it).</summary>
	public Cs2Menu SetItemSubmenu(int item, Cs2Menu? child)
	{
		Cs2MenusNative.SetItemSubmenu(Handle, item, child?.Handle ?? 0);
		return this;
	}

	/// <summary>The submenu a row opens (see <see cref="AddSubMenu"/>), or null if it's a normal item / not tracked by this bridge.</summary>
	public Cs2Menu? GetItemSubmenu(int item)
	{
		uint h = Cs2MenusNative.GetItemSubmenu(Handle, item);
		return h != 0 ? _owner.Find(h) : null;
	}

	// --- Display ---
	public bool Display(int slot, float duration = 0f) => Cs2MenusNative.Display(Handle, slot, duration);
	public void DisplayToAll(float duration = 0f) => Cs2MenusNative.DisplayToAll(Handle, duration);

	// --- Lifetime ---
	/// <summary>True while this menu handle is still live in cs2menus.</summary>
	public bool IsValid => Handle != 0 && Cs2MenusNative.IsValid(Handle);

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
		// A token from an already-freed menu should never reach here (native stops calling back once the menu is destroyed),
		// but recovering a GCHandle from a stale pointer is undefined.
		// Guard so a bad token can't tear down the process.
		try
		{
			var gch = GCHandle.FromIntPtr((nint)user);
			return gch.IsAllocated ? gch.Target as Cs2Menu : null;
		}
		catch
		{
			return null;
		}
	}
}
