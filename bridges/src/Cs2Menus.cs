using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

namespace Cs2Menus;

public enum MenuType { Default = -1, Chat = 0, Html = 1, Panorama = 2 }

public enum MenuEndReason { Selected = 0, Exit = 1, Timeout = 2, Disconnect = 3, Cancelled = 4, Destroyed = 5 }

public enum MenuButton
{
	Default = 0, W, A, S, D, Use, Speed, Duck, Jump, Reload, Attack, Attack2, Score, Inspect, None
}

public enum MenuNavAction { Up = 0, Down = 1, Select = 2, Back = 3 }

public enum MenuLabel { Exit = 0, NextPage = 1, PrevPage = 2, Move = 3, Scroll = 4, Select = 5, On = 6, Off = 7, Adjust = 8, Done = 9 }

/// <summary>See <see cref="Cs2Menu.SetTextFeatures"/>.</summary>
[Flags]
public enum MenuTextFeatures
{
	None = 0,
	/// <summary>ShowConfirm and the unsaved changes question.</summary>
	Confirm = 1,
	/// <summary>Chat: the pinned and secondary item on every page.</summary>
	Pinned = 2,
	/// <summary>A long sorted list opens on its letter ranges.</summary>
	Index = 4,
}

public enum MenuItemType { Normal = 0, Toggle, Stepper, Choice }

/// <summary>Panorama only, each falls back to List when the addon lacks it. Chat and HTML menus are always lists.</summary>
public enum MenuLayout
{
	List = 0,
	/// <summary>Image tiles, sections as tabs.</summary>
	Grid,
	/// <summary>The menu image on the left, items as 3-column buttons, sections as tabs.</summary>
	Showcase,
	/// <summary>Showcase's buttons in a box at the screen's edge, the rest of the screen left to a 3D preview the plugin puts there.</summary>
	Studio,
	/// <summary>A column per section, each scrolling on its own, tiles of an image beside a name and subtext. Up to 4 sections of 16 items.</summary>
	Columns,
}

/// <summary>How Panorama draws an item. Chat and HTML list every role as a plain row.</summary>
public enum MenuItemRole
{
	Button = 0,
	/// <summary>Shown, never picked: its subtext labels its text.</summary>
	Readout,
	/// <summary>The input field over the items, see <see cref="Cs2MenusBridge.BeginInput"/>.</summary>
	Input,
	/// <summary>A label over the items after it, never picked.</summary>
	Heading,
}

/// <summary>A tile's corner button, see <see cref="Cs2Menu.SetItemCorner"/>.</summary>
public enum MenuCorner { None = 0, Star, StarOn, StarUndo, Copy }

/// <summary>A message line's color, and a chip list row's.</summary>
public enum MenuTone { Info = 0, Ok, Warn, Bad }

/// <summary>Team dots on a tile and on the header's scope chip.</summary>
[Flags]
public enum MenuTeams { None = 0, T = 1, CT = 2 }

/// <summary>A grid's smallest tiles, it grows them when the items fit.</summary>
public enum MenuTileSize
{
	/// <summary>6 x 4</summary>
	Small = 0,
	/// <summary>4 x 3</summary>
	Medium,
	/// <summary>3 x 2</summary>
	Large,
	/// <summary>3 x 1, full height</summary>
	Cards,
}

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
	FooterRangeFormat,
	PagePrefixDelimiter,
	// Value items
	ValueFormat,
	EditFormat,
	// Sections
	SectionFormat,
	SectionColor
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

	/// <summary>True if the loaded cs2menus has value items, sections and grids. Otherwise those setters throw <see cref="NotSupportedException"/>.</summary>
	public bool SupportsValueItemsAndGrids => Cs2MenusNative.Supports004;

	/// <summary>
	/// True if it also has the showcase layout, tile sizes, menu images, menu history and pausing.
	/// Otherwise those throw <see cref="NotSupportedException"/>.
	/// </summary>
	public bool SupportsShowcaseAndHistory => Cs2MenusNative.SupportsHistory;

	/// <summary>
	/// True if it also has the ICS2Menus005 additions: item roles, tile badges, the info card, the header's scope chip, tabs,
	/// chips, the studio and columns layouts, typing in the window, the message line and the confirm dialog.
	/// Otherwise their setters throw <see cref="NotSupportedException"/>, and the calls on a slot's display return false.
	/// </summary>
	public bool SupportsBadgesTabsAndDialogs => Cs2MenusNative.Supports005;

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

	/// <summary>Back <paramref name="steps"/> menus in the slot's history. Needs the display open, so not from a close-on-select handler.</summary>
	public static bool StepBack(int slot, int steps = 1) => Cs2MenusNative.Loaded && Cs2MenusNative.StepBack(slot, steps);

	/// <summary>
	/// Hide the slot's menu without ending it, like while the player types in chat. No input meanwhile.
	/// <see cref="Resume"/> brings it back, so does another menu on the slot.
	/// </summary>
	public static void Suspend(int slot)
	{
		if (Cs2MenusNative.Loaded)
		{
			Cs2MenusNative.Suspend(slot);
		}
	}

	public static void Resume(int slot)
	{
		if (Cs2MenusNative.Loaded)
		{
			Cs2MenusNative.Resume(slot);
		}
	}

	/// <summary>What a menu created as <paramref name="type"/> would show as for this player right now: their preference, then what can render.</summary>
	public static MenuType GetSlotType(int slot, MenuType type = MenuType.Default) =>
		Cs2MenusNative.Loaded ? (MenuType)Cs2MenusNative.GetSlotType(slot, (int)type) : type;

	/// <summary>Ends the typing <see cref="BeginInput"/> started, once the chat message came.</summary>
	public static void EndInput(int slot)
	{
		if (Cs2MenusNative.Loaded)
		{
			Cs2MenusNative.EndInput(slot);
		}
	}

	/// <summary>A one-line message under the slot's panorama window. False without one, print it in chat then.</summary>
	public static bool ShowMessage(int slot, string text, MenuTone tone = MenuTone.Info, float seconds = 4f) =>
		Cs2MenusNative.Loaded && Cs2MenusNative.ShowMessage(slot, text, (int)tone, seconds);

	/// <summary>Studio: a part of the hint pill, key caps separated by spaces ("W A S D") and what they do. Up to 5 parts of 4 keys.</summary>
	public static bool AddHint(int slot, string keys, string text) => Cs2MenusNative.Loaded && Cs2MenusNative.AddHint(slot, keys, text);

	/// <summary>The pill back to the menus' own "LMB Turn the view".</summary>
	public static void ClearHint(int slot)
	{
		if (Cs2MenusNative.Loaded)
		{
			Cs2MenusNative.ClearHint(slot);
		}
	}

	/// <summary>No pill at all, for a preview that neither keys nor mouse move.</summary>
	public static void HideHint(int slot)
	{
		if (Cs2MenusNative.Loaded)
		{
			Cs2MenusNative.HideHint(slot);
		}
	}

	/// <summary>Studio: a row of the key list behind the control panel's "?" button. Up to 8 rows of 3 keys.</summary>
	public static bool AddHelp(int slot, string keys, string text) => Cs2MenusNative.Loaded && Cs2MenusNative.AddHelp(slot, keys, text);

	public static void ClearHelp(int slot)
	{
		if (Cs2MenusNative.Loaded)
		{
			Cs2MenusNative.ClearHelp(slot);
		}
	}

	/// <summary>Studio: the box at the screen's left edge and the control panel at its right, until the display ends.</summary>
	public static void SetMirrored(int slot, bool mirrored)
	{
		if (Cs2MenusNative.Loaded)
		{
			Cs2MenusNative.SetMirrored(slot, mirrored);
		}
	}

	// Callbacks for what's on a slot's display rather than one menu, by the token handed to cs2menus, with the bridge that
	// made them so its Dispose drops them. A slot has one of each at most.
	private static readonly ConcurrentDictionary<nint, (Cs2MenusBridge Owner, Delegate Callback)> s_displayCallbacks = new();
	private static readonly nint[] s_confirmToken = new nint[65];
	private static readonly nint[] s_inputToken = new nint[65];
	private static long s_nextToken;

	internal static Delegate? TakeDisplayCallback(nint token) => s_displayCallbacks.TryRemove(token, out var kept) ? kept.Callback : null;

	private nint KeepDisplayCallback(nint[] perSlot, int slot, Delegate? callback, out nint previous)
	{
		previous = perSlot[slot];
		nint token = callback is null ? 0 : (nint)System.Threading.Interlocked.Increment(ref s_nextToken);
		if (callback is not null)
		{
			s_displayCallbacks[token] = (this, callback);
		}
		perSlot[slot] = token;
		return token;
	}

	// Taken, the one before it is gone: answered during the call, replaced, or dropped with its display. Refused, it stays.
	private static void SettleDisplayCallback(nint[] perSlot, int slot, nint token, nint previous, bool taken)
	{
		if (taken)
		{
			s_displayCallbacks.TryRemove(previous, out _);
		}
		else
		{
			s_displayCallbacks.TryRemove(token, out _);
			perSlot[slot] = previous;
		}
	}

	/// <summary>
	/// Waits for chat with this bridge's menu up on the slot: its input field shows <paramref name="prompt"/> and, at its end,
	/// <paramref name="hint"/>. A click in the window calls <paramref name="onCancel"/> with the slot. End it with
	/// <see cref="EndInput"/>. False on the list layout, the other menu types or another plugin's menu: suspend the menu instead.
	/// </summary>
	public bool BeginInput(int slot, string prompt, string hint = "", Action<int>? onCancel = null)
	{
		if (slot < 0 || slot >= s_inputToken.Length || GetActiveMenu(slot) is null)
		{
			return false;
		}
		nint token = KeepDisplayCallback(s_inputToken, slot, onCancel, out nint previous);
		bool begun;
		unsafe
		{
			begun = Cs2MenusNative.BeginInput(slot, prompt, hint,
				onCancel is null ? 0 : (nint)(delegate* unmanaged[Cdecl]<uint, int, void*, void>)&Trampolines.OnInputCancel, (void*)token);
		}
		SettleDisplayCallback(s_inputToken, slot, token, previous, begun);
		return begun;
	}

	/// <summary>
	/// A confirm dialog over this bridge's menu on the slot, <paramref name="onDone"/> getting the slot and whether the confirm
	/// button was clicked. A red confirm button when <paramref name="danger"/>. False without a panorama window of this bridge's
	/// shown: go ahead or ask in chat.
	/// </summary>
	public bool ShowConfirm(int slot, string title, string body, string cancel, string confirm, bool danger = false, Action<int, bool>? onDone = null)
	{
		if (slot < 0 || slot >= s_confirmToken.Length || GetActiveMenu(slot) is null)
		{
			return false;
		}
		nint token = KeepDisplayCallback(s_confirmToken, slot, onDone, out nint previous);
		bool shown;
		unsafe
		{
			shown = Cs2MenusNative.ShowConfirm(slot, title, body, cancel, confirm, danger,
				onDone is null ? 0 : (nint)(delegate* unmanaged[Cdecl]<int, int, void*, void>)&Trampolines.OnConfirm, (void*)token);
		}
		SettleDisplayCallback(s_confirmToken, slot, token, previous, shown);
		return shown;
	}

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
			Cs2MenusNative.SetChangeCallback(handle, (nint)(delegate* unmanaged[Cdecl]<uint, int, int, int, void*, void>)&Trampolines.OnChange, menu.Token);
			Cs2MenusNative.SetCornerCallback(handle, (nint)(delegate* unmanaged[Cdecl]<uint, int, int, void*, void>)&Trampolines.OnCorner, menu.Token);
			Cs2MenusNative.SetTabCallback(handle, (nint)(delegate* unmanaged[Cdecl]<uint, int, int, void*, void>)&Trampolines.OnTab, menu.Token);
			Cs2MenusNative.SetChipCallback(handle, (nint)(delegate* unmanaged[Cdecl]<uint, int, int, int, void*, void>)&Trampolines.OnChip, menu.Token);
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
		// Their dialogs went with the menus, unanswered. Kept, they would hold on to the unloading plugin.
		foreach (var (token, kept) in s_displayCallbacks)
		{
			if (kept.Owner == this)
			{
				s_displayCallbacks.TryRemove(token, out _);
				s_confirmToken.AsSpan().Replace(token, 0);
				s_inputToken.AsSpan().Replace(token, 0);
			}
		}
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

	/// <summary>A value item changed: (menu, slot, item, value), already stored. <see cref="SetItemValue"/> in the handler overrides it.</summary>
	public event Action<Cs2Menu, int, int, int>? Changed;

	private Action<Cs2Menu, int>? _refreshed;

	/// <summary>
	/// The player pressed the Panorama refresh button: (menu, slot). Rebuild the items here.
	/// The button only shows while this has a handler.
	/// </summary>
	public event Action<Cs2Menu, int>? Refreshed
	{
		add => Subscribe(ref _refreshed, value, true, WireRefresh);
		remove => Subscribe(ref _refreshed, value, false, WireRefresh);
	}

	/// <summary>A tile's corner button was clicked instead of the tile: (menu, slot, item). <see cref="SetItemCorner"/> sets the new state.</summary>
	public event Action<Cs2Menu, int, int>? CornerClicked;

	/// <summary>One of the menu's own tabs was clicked: (menu, slot, tab). Show what it stands for, usually with <see cref="Replace"/>.</summary>
	public event Action<Cs2Menu, int, int>? TabClicked;

	/// <summary>
	/// A chip was clicked: (menu, slot, chip, selected). A filter's selected is already stored: the option picked, -1 for none,
	/// or 1 and 0 without options. An action's is the option picked from its list, 0 without one.
	/// </summary>
	public event Action<Cs2Menu, int, int, int>? ChipClicked;

	private Action<Cs2Menu, int>? _scopeClicked;

	/// <summary>The header's scope chip was clicked: (menu, slot). It only is a button while this has a handler.</summary>
	public event Action<Cs2Menu, int>? ScopeClicked
	{
		add => Subscribe(ref _scopeClicked, value, true, WireScope);
		remove => Subscribe(ref _scopeClicked, value, false, WireScope);
	}

	private Action<Cs2Menu, int>? _inputCleared;

	/// <summary>The input field's clear button was clicked: (menu, slot). The button only shows while this has a handler and the field has text.</summary>
	public event Action<Cs2Menu, int>? InputCleared
	{
		add => Subscribe(ref _inputCleared, value, true, WireInputClear);
		remove => Subscribe(ref _inputCleared, value, false, WireInputClear);
	}

	// cs2menus shows the button behind each of these only while its callback is set: from the first handler until the last goes.
	private void Subscribe(ref Action<Cs2Menu, int>? handlers, Action<Cs2Menu, int>? handler, bool add, Action<bool> wire)
	{
		bool had = handlers is not null;
		handlers = add ? handlers + handler : handlers - handler;
		if (had != (handlers is not null) && Handle != 0)
		{
			wire(handlers is not null);
		}
	}

	private unsafe void WireRefresh(bool on) =>
		Cs2MenusNative.SetRefreshCallback(Handle, on ? (nint)(delegate* unmanaged[Cdecl]<uint, int, void*, void>)&Trampolines.OnRefresh : 0, on ? Token : null);

	private unsafe void WireScope(bool on) =>
		Cs2MenusNative.SetScopeCallback(Handle, on ? (nint)(delegate* unmanaged[Cdecl]<uint, int, void*, void>)&Trampolines.OnScope : 0, on ? Token : null);

	private unsafe void WireInputClear(bool on) =>
		Cs2MenusNative.SetInputClearCallback(Handle, on ? (nint)(delegate* unmanaged[Cdecl]<uint, int, void*, void>)&Trampolines.OnInputClear : 0, on ? Token : null);

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
	internal void RaiseChanged(int slot, int item, int value) => Changed?.Invoke(this, slot, item, value);
	internal void RaiseRefreshed(int slot) => _refreshed?.Invoke(this, slot);
	internal void RaiseCorner(int slot, int item) => CornerClicked?.Invoke(this, slot, item);
	internal void RaiseTab(int slot, int tab) => TabClicked?.Invoke(this, slot, tab);
	internal void RaiseChip(int slot, int chip, int selected) => ChipClicked?.Invoke(this, slot, chip, selected);
	internal void RaiseScope(int slot) => _scopeClicked?.Invoke(this, slot);
	internal void RaiseInputCleared(int slot) => _inputCleared?.Invoke(this, slot);

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

	// --- Value items, reported through Changed. Each Add returns the index, or -1 ---

	public int AddToggle(string text, bool on, string info = "") => Cs2MenusNative.AddToggle(Handle, text, on, info);
	/// <summary>An integer in [min, max] moved by step. min > max are swapped, a step below 1 becomes 1, and value is clamped.</summary>
	public int AddStepper(string text, int value, int min, int max, int step = 1, string info = "") =>
		Cs2MenusNative.AddStepper(Handle, text, value, min, max, step, info);
	/// <summary>One of <paramref name="options"/>. The value is the selected option's index.</summary>
	public int AddChoice(string text, IReadOnlyList<string> options, int selected = 0, string info = "") =>
		Cs2MenusNative.AddChoice(Handle, text, options, selected, info);
	public MenuItemType GetItemType(int item) => (MenuItemType)Cs2MenusNative.GetItemType(Handle, item);
	/// <summary>Toggle 0/1, stepper value, choice index. Clamped, doesn't raise <see cref="Changed"/>.</summary>
	public void SetItemValue(int item, int value) => Cs2MenusNative.SetItemValue(Handle, item, value);
	public int GetItemValue(int item) => Cs2MenusNative.GetItemValue(Handle, item);

	// --- Sections and grids ---

	/// <summary>Groups the items added after it. Panorama shows sections as the left column or grid tabs, chat and HTML as headers. Returns the index, or -1.</summary>
	public int AddSection(string name) => Cs2MenusNative.AddSection(Handle, name);
	/// <summary>The item's section index, or -1 for none.</summary>
	public int GetItemSection(int item) => Cs2MenusNative.GetItemSection(Handle, item);
	/// <summary>Panorama only. Grid falls back to List without the addon's grid layout.</summary>
	public Cs2Menu SetLayout(MenuLayout layout) { Cs2MenusNative.SetMenuLayout(Handle, (int)layout); return this; }
	public MenuLayout Layout => (MenuLayout)Cs2MenusNative.GetMenuLayout(Handle);
	/// <summary>Grid tile icon from the game's panorama/images/icons/equipment, like "ak47". "" removes it.</summary>
	public void SetItemImage(int item, string image) => Cs2MenusNative.SetItemImage(Handle, item, image);
	public string GetItemImage(int item) => Cs2MenusNative.GetItemImage(Handle, item);
	/// <summary>Secondary text, like a price. A value item shows its value instead.</summary>
	public void SetItemSubtext(int item, string subtext) => Cs2MenusNative.SetItemSubtext(Handle, item, subtext);
	public string GetItemSubtext(int item) => Cs2MenusNative.GetItemSubtext(Handle, item);

	// --- Panorama layouts ---

	/// <summary>A grid's smallest tiles, default Small. It grows them when the items fit.</summary>
	public Cs2Menu SetTileSize(MenuTileSize size) { Cs2MenusNative.SetMenuTileSize(Handle, (int)size); return this; }
	public MenuTileSize TileSize => (MenuTileSize)Cs2MenusNative.GetMenuTileSize(Handle);
	/// <summary>Shown inside a showcase, beside the box otherwise. An addon image class like <see cref="SetItemImage"/>'s. "" removes it.</summary>
	public Cs2Menu SetImage(string image) { Cs2MenusNative.SetMenuImage(Handle, image); return this; }
	public string Image => Cs2MenusNative.GetMenuImage(Handle);
	/// <summary>Showcase: the item shown as a wide button under the image on every page, -1 for none. Set after adding the items.</summary>
	public Cs2Menu SetPinnedItem(int item) { Cs2MenusNative.SetMenuPinnedItem(Handle, item); return this; }
	public int PinnedItem => Cs2MenusNative.GetMenuPinnedItem(Handle);
	/// <summary>Panorama features for the menu's chat and HTML displays, none by default. Does nothing on cs2menus 2.0.0.</summary>
	public Cs2Menu SetTextFeatures(MenuTextFeatures features) { Cs2MenusNative.SetMenuTextFeatures(Handle, (int)features); return this; }
	public MenuTextFeatures TextFeatures => (MenuTextFeatures)Cs2MenusNative.GetMenuTextFeatures(Handle);

	// --- Item presentation, Panorama only ---

	public void SetItemRole(int item, MenuItemRole role) => Cs2MenusNative.SetItemRole(Handle, item, (int)role);
	public MenuItemRole GetItemRole(int item) => (MenuItemRole)Cs2MenusNative.GetItemRole(Handle, item);
	/// <summary>Marked with the accent: the equipped pick, a mode that's on.</summary>
	public void SetItemHighlight(int item, bool highlight) => Cs2MenusNative.SetItemHighlight(Handle, item, highlight);
	/// <summary>Showcase and studio: how many of the three columns the button takes. Columns: 2 for its column's width, 1 for half.</summary>
	public void SetItemSpan(int item, int columns) => Cs2MenusNative.SetItemSpan(Handle, item, columns);
	/// <summary>Studio: the item sits in the control panel beside the preview, on every page. Its section is its tab there.</summary>
	public void SetItemControl(int item, bool control) => Cs2MenusNative.SetItemControl(Handle, item, control);

	// --- Tile badges, image tiles only ---

	/// <summary>A stripe in one of the game's rarity colors: consumer, industrial, milspec, restricted, classified, covert, contraband. "" for none.</summary>
	public void SetItemRarity(int item, string rarity) => Cs2MenusNative.SetItemRarity(Handle, item, rarity);
	public string GetItemRarity(int item) => Cs2MenusNative.GetItemRarity(Handle, item);
	/// <summary>A short label at the tile's top left, like "ST". <paramref name="style"/> is "orange", "gold" or "" for the plain look.</summary>
	public void SetItemTag(int item, string tag, string style = "") => Cs2MenusNative.SetItemTag(Handle, item, tag, style);
	public string GetItemTag(int item) => Cs2MenusNative.GetItemTag(Handle, item);
	public void SetItemTeams(int item, MenuTeams teams) => Cs2MenusNative.SetItemTeams(Handle, item, (int)teams);
	public MenuTeams GetItemTeams(int item) => (MenuTeams)Cs2MenusNative.GetItemTeams(Handle, item);
	/// <summary>A lock on the tile. Picking it still fires the select handler, so the plugin can say why.</summary>
	public void SetItemLocked(int item, bool locked) => Cs2MenusNative.SetItemLocked(Handle, item, locked);
	public bool GetItemLocked(int item) => Cs2MenusNative.GetItemLocked(Handle, item);
	/// <summary>A button of its own at the tile's top right, its clicks raise <see cref="CornerClicked"/>.</summary>
	public void SetItemCorner(int item, MenuCorner corner) => Cs2MenusNative.SetItemCorner(Handle, item, (int)corner);
	public MenuCorner GetItemCorner(int item) => (MenuCorner)Cs2MenusNative.GetItemCorner(Handle, item);
	/// <summary>The tile's image washed in a color the addon has a class for, like a graffiti tint's id. "" for none.</summary>
	public void SetItemImageTint(int item, string tint) => Cs2MenusNative.SetItemImageTint(Handle, item, tint);

	// --- Info card, showcase and studio ---

	/// <summary>"" title hides the card. <paramref name="subtitleColor"/> is "#RRGGBB", a rarity as in <see cref="SetItemRarity"/>, or "".</summary>
	public Cs2Menu SetInfo(string title, string subtitle = "", string subtitleColor = "")
	{
		Cs2MenusNative.SetMenuInfo(Handle, title, subtitle, subtitleColor);
		return this;
	}

	/// <summary>
	/// A 0 to 1 bar with a marker at <paramref name="value"/>, dimmed outside the range, the label and value text under it.
	/// <paramref name="bands"/> splits it at each band's upper end, at most 5. A negative value hides the bar.
	/// </summary>
	public Cs2Menu SetInfoMeter(float value, float rangeMin = 0f, float rangeMax = 1f, float[]? bands = null, string label = "", string valueText = "")
	{
		Cs2MenusNative.SetMenuInfoMeter(Handle, value, rangeMin, rangeMax, bands, label, valueText);
		return this;
	}

	/// <summary>A label and value pair under the bar, at most 10. Returns the index, or -1.</summary>
	public int AddInfoRow(string label, string value) => Cs2MenusNative.AddMenuInfoRow(Handle, label, value);
	public void ClearInfo() => Cs2MenusNative.ClearMenuInfo(Handle);

	// --- The header ---

	/// <summary>A chip after the title saying what the menu's picks go to, like "T side", with team dots. "" hides it.</summary>
	public Cs2Menu SetScope(string label, MenuTeams teams = MenuTeams.None)
	{
		Cs2MenusNative.SetMenuScope(Handle, label, (int)teams);
		return this;
	}

	/// <summary>Unsaved changes: an Edited marker after the title, and closing or backing out asks first.</summary>
	public bool Edited { get => Cs2MenusNative.GetMenuEdited(Handle); set => Cs2MenusNative.SetMenuEdited(Handle, value); }

	// --- Tabs and chips, every layout but the list ---

	/// <summary>A tab of the plugin's own in place of the sections'. Returns its index, or -1 past 10.</summary>
	public int AddTab(string label, bool selected = false, bool marked = false, bool pinned = false) =>
		Cs2MenusNative.AddMenuTab(Handle, label, selected, marked, pinned);

	/// <summary>
	/// A filter chip under the tabs. With options a click lists them and <paramref name="selected"/> is the one picked, -1 for none.
	/// Without, a click toggles it: 1 on, 0 off. Returns its index, or -1 past the row's room.
	/// </summary>
	public int AddChip(string label, IReadOnlyList<string>? options = null, int selected = -1) =>
		Cs2MenusNative.AddMenuChip(Handle, label, options, selected, false);

	/// <summary>A button in the chip row, or with options a list of them. It keeps nothing. <paramref name="accent"/> draws it lit.</summary>
	public int AddAction(string label, IReadOnlyList<string>? options = null, bool accent = false) =>
		Cs2MenusNative.AddMenuChip(Handle, label, options, accent ? 1 : 0, true);

	/// <summary>A chip that only says something: the label, then the value.</summary>
	public int AddNote(string label, string value) => Cs2MenusNative.AddMenuNote(Handle, label, value);
	/// <summary>A row of a chip's list in a tone's color, like Bad for one that deletes.</summary>
	public void SetChipOptionTone(int chip, int option, MenuTone tone) => Cs2MenusNative.SetChipOptionTone(Handle, chip, option, (int)tone);

	// --- The item area ---

	/// <summary>Showcase and studio: a small button before the pinned item's, on every page. -1 for none. Set after the items.</summary>
	public Cs2Menu SetSecondaryItem(int item) { Cs2MenusNative.SetMenuSecondaryItem(Handle, item); return this; }

	/// <summary>What the item area says while the page shown has no items. <paramref name="loading"/> pulses it.</summary>
	public Cs2Menu SetEmpty(string title, string text = "", bool loading = false)
	{
		Cs2MenusNative.SetMenuEmpty(Handle, title, text, loading);
		return this;
	}

	// --- Display ---
	public bool Display(int slot, float duration = 0f) => Cs2MenusNative.Display(Handle, slot, duration);
	public void DisplayToAll(float duration = 0f) => Cs2MenusNative.DisplayToAll(Handle, duration);

	/// <summary>
	/// Show on top of the slot's current menu, which the back arrow returns to. Works from a close-on-select handler.
	/// Clears the forward history and keeps the current display's timeout.
	/// </summary>
	public bool Push(int slot, float duration = 0f) => Cs2MenusNative.Push(Handle, slot, duration);
	/// <summary>Show in place of the slot's current menu, which ends (Cancelled).</summary>
	public bool Replace(int slot, float duration = 0f) => Cs2MenusNative.Replace(Handle, slot, duration);

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

	[UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
	public static void OnChange(uint menu, int slot, int item, int value, void* user)
	{
		var m = Resolve(user);
		if (m is null)
		{
			return;
		}
		try
		{
			m.RaiseChanged(slot, item, value);
		}
		catch
		{
		}
	}

	[UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
	public static void OnRefresh(uint menu, int slot, void* user)
	{
		var m = Resolve(user);
		if (m is null)
		{
			return;
		}
		try
		{
			m.RaiseRefreshed(slot);
		}
		catch
		{
		}
	}

	[UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
	public static void OnCorner(uint menu, int slot, int item, void* user)
	{
		try
		{
			Resolve(user)?.RaiseCorner(slot, item);
		}
		catch
		{
		}
	}

	[UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
	public static void OnTab(uint menu, int slot, int tab, void* user)
	{
		try
		{
			Resolve(user)?.RaiseTab(slot, tab);
		}
		catch
		{
		}
	}

	[UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
	public static void OnChip(uint menu, int slot, int chip, int selected, void* user)
	{
		try
		{
			Resolve(user)?.RaiseChip(slot, chip, selected);
		}
		catch
		{
		}
	}

	[UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
	public static void OnScope(uint menu, int slot, void* user)
	{
		try
		{
			Resolve(user)?.RaiseScope(slot);
		}
		catch
		{
		}
	}

	[UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
	public static void OnInputClear(uint menu, int slot, void* user)
	{
		try
		{
			Resolve(user)?.RaiseInputCleared(slot);
		}
		catch
		{
		}
	}

	// The display's callbacks: `user` is the token their delegate is kept under, see Cs2MenusBridge.BeginInput.
	[UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
	public static void OnInputCancel(uint menu, int slot, void* user)
	{
		try
		{
			(Cs2MenusBridge.TakeDisplayCallback((nint)user) as Action<int>)?.Invoke(slot);
		}
		catch
		{
		}
	}

	[UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
	public static void OnConfirm(int slot, int confirmed, void* user)
	{
		try
		{
			(Cs2MenusBridge.TakeDisplayCallback((nint)user) as Action<int, bool>)?.Invoke(slot, confirmed != 0);
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
