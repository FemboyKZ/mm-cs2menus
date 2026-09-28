using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;

namespace Cs2Menus;

/// <summary>
/// Raw bindings to the cs2menus flat C ABI (cs2menus_capi.h).
///
/// Framework-agnostic: shared by the SwiftlyS2 and CounterStrikeSharp wrapper packages.
/// The cs2menus binary lives in the Metamod addons folder, not on the default library search path,
/// so we resolve it explicitly via <see cref="Load"/> and cache function pointers, rather than [DllImport].
///
/// All functions are thread-safe (cs2menus is). Callbacks always arrive on the game main thread.
///
/// Members below mirror the ICS2Menus interface order: Handshake, Lifetime, Menu properties, Items, Display, Host.
/// </summary>
internal static unsafe class Cs2MenusNative
{
	// Handshake
	private static delegate* unmanaged[Cdecl]<int> _abiVersion;
	private static delegate* unmanaged[Cdecl]<int> _available;

	// Lifetime
	private static delegate* unmanaged[Cdecl]<int, byte*, nint, void*, uint> _create;
	private static delegate* unmanaged[Cdecl]<uint, void> _destroy;
	private static delegate* unmanaged[Cdecl]<uint, int> _isValid;

	// Menu properties
	private static delegate* unmanaged[Cdecl]<uint, byte*, void> _setTitle;
	private static delegate* unmanaged[Cdecl]<uint, byte*, int, int> _getTitle;
	private static delegate* unmanaged[Cdecl]<uint, int> _getMenuType;
	private static delegate* unmanaged[Cdecl]<uint, int, void> _setExitButton;
	private static delegate* unmanaged[Cdecl]<uint, int> _getExitButton;
	private static delegate* unmanaged[Cdecl]<uint, int, void> _setCloseOnSelect;
	private static delegate* unmanaged[Cdecl]<uint, int> _getCloseOnSelect;
	private static delegate* unmanaged[Cdecl]<uint, int, void> _setExitItem;
	private static delegate* unmanaged[Cdecl]<uint, int> _getExitItem;
	private static delegate* unmanaged[Cdecl]<uint, int, void> _setForceType;
	private static delegate* unmanaged[Cdecl]<uint, int> _getForceType;
	private static delegate* unmanaged[Cdecl]<uint, int, void> _setStartItem;
	private static delegate* unmanaged[Cdecl]<uint, int> _getStartItem;
	private static delegate* unmanaged[Cdecl]<uint, nint, void*, void> _setEndCallback;
	private static delegate* unmanaged[Cdecl]<uint, int, int, void> _setMenuKey;
	private static delegate* unmanaged[Cdecl]<uint, int, int> _getMenuKey;
	private static delegate* unmanaged[Cdecl]<uint, int, byte*, void> _setMenuLabel;
	private static delegate* unmanaged[Cdecl]<uint, int, byte*, int, int> _getMenuLabel;
	private static delegate* unmanaged[Cdecl]<uint, int, byte*, void> _setMenuStyle;
	private static delegate* unmanaged[Cdecl]<uint, int, byte*, int, int> _getMenuStyle;

	// Items
	private static delegate* unmanaged[Cdecl]<uint, byte*, byte*, int, int> _addItem;
	private static delegate* unmanaged[Cdecl]<uint, int, byte*, byte*, int, int> _insertItem;
	private static delegate* unmanaged[Cdecl]<uint, byte*, uint, byte*, int> _addSubmenu;
	private static delegate* unmanaged[Cdecl]<uint, int, void> _removeItem;
	private static delegate* unmanaged[Cdecl]<uint, void> _removeAllItems;
	private static delegate* unmanaged[Cdecl]<uint, int> _itemCount;
	private static delegate* unmanaged[Cdecl]<uint, int, byte*, void> _setItemText;
	private static delegate* unmanaged[Cdecl]<uint, int, byte*, int, int> _getItemText;
	private static delegate* unmanaged[Cdecl]<uint, int, byte*, void> _setItemInfo;
	private static delegate* unmanaged[Cdecl]<uint, int, byte*, int, int> _getItemInfo;
	private static delegate* unmanaged[Cdecl]<uint, int, int, void> _setItemDisabled;
	private static delegate* unmanaged[Cdecl]<uint, int, int> _getItemDisabled;
	private static delegate* unmanaged[Cdecl]<uint, int, int, void> _setItemRaw;
	private static delegate* unmanaged[Cdecl]<uint, int, int> _getItemRaw;
	private static delegate* unmanaged[Cdecl]<uint, int, byte*, void> _setItemIcon;
	private static delegate* unmanaged[Cdecl]<uint, int, byte*, int, int> _getItemIcon;
	private static delegate* unmanaged[Cdecl]<uint, int, uint, void> _setItemSubmenu;
	private static delegate* unmanaged[Cdecl]<uint, int, uint> _getItemSubmenu;

	// Display
	private static delegate* unmanaged[Cdecl]<uint, int, float, int> _display;
	private static delegate* unmanaged[Cdecl]<uint, float, void> _displayToAll;
	private static delegate* unmanaged[Cdecl]<int, void> _cancel;
	private static delegate* unmanaged[Cdecl]<int, int> _hasMenu;
	private static delegate* unmanaged[Cdecl]<int, uint> _getActiveMenu;
	private static delegate* unmanaged[Cdecl]<int, int> _getActiveType;
	private static delegate* unmanaged[Cdecl]<int, int> _getSelectedItem;

	// Host coordination
	private static delegate* unmanaged[Cdecl]<int, int, void> _setExternalBusy;
	private static delegate* unmanaged[Cdecl]<int, int> _getExternalBusy;

	// Value items
	private static delegate* unmanaged[Cdecl]<uint, byte*, int, byte*, int> _addToggle;
	private static delegate* unmanaged[Cdecl]<uint, byte*, int, int, int, int, byte*, int> _addStepper;
	private static delegate* unmanaged[Cdecl]<uint, byte*, nint*, int, int, byte*, int> _addChoice;
	private static delegate* unmanaged[Cdecl]<uint, int, int> _getItemType;
	private static delegate* unmanaged[Cdecl]<uint, int, int, void> _setItemValue;
	private static delegate* unmanaged[Cdecl]<uint, int, int> _getItemValue;
	private static delegate* unmanaged[Cdecl]<uint, nint, void*, void> _setChangeCallback;

	// Sections and grids
	private static delegate* unmanaged[Cdecl]<uint, byte*, int> _addSection;
	private static delegate* unmanaged[Cdecl]<uint, int, int> _getItemSection;
	private static delegate* unmanaged[Cdecl]<uint, int, void> _setMenuLayout;
	private static delegate* unmanaged[Cdecl]<uint, int> _getMenuLayout;
	private static delegate* unmanaged[Cdecl]<uint, int, byte*, void> _setItemImage;
	private static delegate* unmanaged[Cdecl]<uint, int, byte*, int, int> _getItemImage;
	private static delegate* unmanaged[Cdecl]<uint, int, byte*, void> _setItemSubtext;
	private static delegate* unmanaged[Cdecl]<uint, int, byte*, int, int> _getItemSubtext;

	// Panorama layouts
	private static delegate* unmanaged[Cdecl]<uint, int, void> _setMenuTileSize;
	private static delegate* unmanaged[Cdecl]<uint, int> _getMenuTileSize;
	private static delegate* unmanaged[Cdecl]<uint, byte*, void> _setMenuImage;
	private static delegate* unmanaged[Cdecl]<uint, byte*, int, int> _getMenuImage;
	private static delegate* unmanaged[Cdecl]<uint, int, void> _setMenuPinnedItem;
	private static delegate* unmanaged[Cdecl]<uint, int> _getMenuPinnedItem;

	// History
	private static delegate* unmanaged[Cdecl]<uint, int, float, int> _push;
	private static delegate* unmanaged[Cdecl]<uint, int, float, int> _replace;
	private static delegate* unmanaged[Cdecl]<int, int, int> _stepBack;
	private static delegate* unmanaged[Cdecl]<uint, nint, void*, void> _setRefreshCallback;

	// Pausing a display
	private static delegate* unmanaged[Cdecl]<int, void> _suspend;
	private static delegate* unmanaged[Cdecl]<int, void> _resume;

	public static bool Loaded { get; private set; }

	/// <summary>True when the loaded cs2menus exports the ICS2Menus004 additions (value items, sections, grids).</summary>
	public static bool Supports004 { get; private set; }

	/// <summary>True when it also exports the later 004 additions: showcase, tile sizes, menu image, history and pausing.</summary>
	public static bool SupportsHistory { get; private set; }

	/// <summary>
	/// Resolve every export from the cs2menus binary at <paramref name="binaryPath"/>.
	/// Call once, before use. Returns false if the library or any export is missing, or the ABI mismatches.
	/// </summary>
	public static bool Load(string binaryPath)
	{
		if (Loaded)
		{
			return true;
		}
		if (!NativeLibrary.TryLoad(binaryPath, out nint lib))
		{
			return false;
		}

		try
		{
			// Handshake
			_abiVersion = (delegate* unmanaged[Cdecl]<int>)Get(lib, "cs2m_abi_version");
			_available = (delegate* unmanaged[Cdecl]<int>)Get(lib, "cs2m_available");
			// Lifetime
			_create = (delegate* unmanaged[Cdecl]<int, byte*, nint, void*, uint>)Get(lib, "cs2m_create");
			_destroy = (delegate* unmanaged[Cdecl]<uint, void>)Get(lib, "cs2m_destroy");
			_isValid = (delegate* unmanaged[Cdecl]<uint, int>)Get(lib, "cs2m_is_valid");
			// Menu properties
			_setTitle = (delegate* unmanaged[Cdecl]<uint, byte*, void>)Get(lib, "cs2m_set_title");
			_getTitle = (delegate* unmanaged[Cdecl]<uint, byte*, int, int>)Get(lib, "cs2m_get_title");
			_getMenuType = (delegate* unmanaged[Cdecl]<uint, int>)Get(lib, "cs2m_get_menu_type");
			_setExitButton = (delegate* unmanaged[Cdecl]<uint, int, void>)Get(lib, "cs2m_set_exit_button");
			_getExitButton = (delegate* unmanaged[Cdecl]<uint, int>)Get(lib, "cs2m_get_exit_button");
			_setCloseOnSelect = (delegate* unmanaged[Cdecl]<uint, int, void>)Get(lib, "cs2m_set_close_on_select");
			_getCloseOnSelect = (delegate* unmanaged[Cdecl]<uint, int>)Get(lib, "cs2m_get_close_on_select");
			_setExitItem = (delegate* unmanaged[Cdecl]<uint, int, void>)Get(lib, "cs2m_set_exit_item");
			_getExitItem = (delegate* unmanaged[Cdecl]<uint, int>)Get(lib, "cs2m_get_exit_item");
			_setForceType = (delegate* unmanaged[Cdecl]<uint, int, void>)Get(lib, "cs2m_set_force_type");
			_getForceType = (delegate* unmanaged[Cdecl]<uint, int>)Get(lib, "cs2m_get_force_type");
			_setStartItem = (delegate* unmanaged[Cdecl]<uint, int, void>)Get(lib, "cs2m_set_start_item");
			_getStartItem = (delegate* unmanaged[Cdecl]<uint, int>)Get(lib, "cs2m_get_start_item");
			_setEndCallback = (delegate* unmanaged[Cdecl]<uint, nint, void*, void>)Get(lib, "cs2m_set_end_callback");
			_setMenuKey = (delegate* unmanaged[Cdecl]<uint, int, int, void>)Get(lib, "cs2m_set_menu_key");
			_getMenuKey = (delegate* unmanaged[Cdecl]<uint, int, int>)Get(lib, "cs2m_get_menu_key");
			_setMenuLabel = (delegate* unmanaged[Cdecl]<uint, int, byte*, void>)Get(lib, "cs2m_set_menu_label");
			_getMenuLabel = (delegate* unmanaged[Cdecl]<uint, int, byte*, int, int>)Get(lib, "cs2m_get_menu_label");
			_setMenuStyle = (delegate* unmanaged[Cdecl]<uint, int, byte*, void>)Get(lib, "cs2m_set_menu_style");
			_getMenuStyle = (delegate* unmanaged[Cdecl]<uint, int, byte*, int, int>)Get(lib, "cs2m_get_menu_style");
			// Items
			_addItem = (delegate* unmanaged[Cdecl]<uint, byte*, byte*, int, int>)Get(lib, "cs2m_add_item");
			_insertItem = (delegate* unmanaged[Cdecl]<uint, int, byte*, byte*, int, int>)Get(lib, "cs2m_insert_item");
			_addSubmenu = (delegate* unmanaged[Cdecl]<uint, byte*, uint, byte*, int>)Get(lib, "cs2m_add_submenu");
			_removeItem = (delegate* unmanaged[Cdecl]<uint, int, void>)Get(lib, "cs2m_remove_item");
			_removeAllItems = (delegate* unmanaged[Cdecl]<uint, void>)Get(lib, "cs2m_remove_all_items");
			_itemCount = (delegate* unmanaged[Cdecl]<uint, int>)Get(lib, "cs2m_item_count");
			_setItemText = (delegate* unmanaged[Cdecl]<uint, int, byte*, void>)Get(lib, "cs2m_set_item_text");
			_getItemText = (delegate* unmanaged[Cdecl]<uint, int, byte*, int, int>)Get(lib, "cs2m_get_item_text");
			_setItemInfo = (delegate* unmanaged[Cdecl]<uint, int, byte*, void>)Get(lib, "cs2m_set_item_info");
			_getItemInfo = (delegate* unmanaged[Cdecl]<uint, int, byte*, int, int>)Get(lib, "cs2m_get_item_info");
			_setItemDisabled = (delegate* unmanaged[Cdecl]<uint, int, int, void>)Get(lib, "cs2m_set_item_disabled");
			_getItemDisabled = (delegate* unmanaged[Cdecl]<uint, int, int>)Get(lib, "cs2m_get_item_disabled");
			_setItemRaw = (delegate* unmanaged[Cdecl]<uint, int, int, void>)Get(lib, "cs2m_set_item_raw");
			_getItemRaw = (delegate* unmanaged[Cdecl]<uint, int, int>)Get(lib, "cs2m_get_item_raw");
			_setItemIcon = (delegate* unmanaged[Cdecl]<uint, int, byte*, void>)Get(lib, "cs2m_set_item_icon");
			_getItemIcon = (delegate* unmanaged[Cdecl]<uint, int, byte*, int, int>)Get(lib, "cs2m_get_item_icon");
			_setItemSubmenu = (delegate* unmanaged[Cdecl]<uint, int, uint, void>)Get(lib, "cs2m_set_item_submenu");
			_getItemSubmenu = (delegate* unmanaged[Cdecl]<uint, int, uint>)Get(lib, "cs2m_get_item_submenu");
			// Display
			_display = (delegate* unmanaged[Cdecl]<uint, int, float, int>)Get(lib, "cs2m_display");
			_displayToAll = (delegate* unmanaged[Cdecl]<uint, float, void>)Get(lib, "cs2m_display_to_all");
			_cancel = (delegate* unmanaged[Cdecl]<int, void>)Get(lib, "cs2m_cancel");
			_hasMenu = (delegate* unmanaged[Cdecl]<int, int>)Get(lib, "cs2m_has_menu");
			_getActiveMenu = (delegate* unmanaged[Cdecl]<int, uint>)Get(lib, "cs2m_get_active_menu");
			_getActiveType = (delegate* unmanaged[Cdecl]<int, int>)Get(lib, "cs2m_get_active_type");
			_getSelectedItem = (delegate* unmanaged[Cdecl]<int, int>)Get(lib, "cs2m_get_selected_item");
			// Host coordination
			_setExternalBusy = (delegate* unmanaged[Cdecl]<int, int, void>)Get(lib, "cs2m_set_external_busy");
			_getExternalBusy = (delegate* unmanaged[Cdecl]<int, int>)Get(lib, "cs2m_get_external_busy");
		}
		catch (EntryPointNotFoundException)
		{
			return false;
		}

		// Gate on ABI: refuse a cs2menus whose facade changed incompatibly.
		if (_abiVersion() != 2)
		{
			return false;
		}

		// Optional, so an older cs2menus still loads.
		if (TryGet(lib, "cs2m_add_toggle", out nint addToggle)
			&& TryGet(lib, "cs2m_add_stepper", out nint addStepper)
			&& TryGet(lib, "cs2m_add_choice", out nint addChoice)
			&& TryGet(lib, "cs2m_get_item_type", out nint getItemType)
			&& TryGet(lib, "cs2m_set_item_value", out nint setItemValue)
			&& TryGet(lib, "cs2m_get_item_value", out nint getItemValue)
			&& TryGet(lib, "cs2m_set_change_callback", out nint setChangeCallback)
			&& TryGet(lib, "cs2m_add_section", out nint addSection)
			&& TryGet(lib, "cs2m_get_item_section", out nint getItemSection)
			&& TryGet(lib, "cs2m_set_menu_layout", out nint setMenuLayout)
			&& TryGet(lib, "cs2m_get_menu_layout", out nint getMenuLayout)
			&& TryGet(lib, "cs2m_set_item_image", out nint setItemImage)
			&& TryGet(lib, "cs2m_get_item_image", out nint getItemImage)
			&& TryGet(lib, "cs2m_set_item_subtext", out nint setItemSubtext)
			&& TryGet(lib, "cs2m_get_item_subtext", out nint getItemSubtext))
		{
			Supports004 = true;
			_addToggle = (delegate* unmanaged[Cdecl]<uint, byte*, int, byte*, int>)addToggle;
			_addStepper = (delegate* unmanaged[Cdecl]<uint, byte*, int, int, int, int, byte*, int>)addStepper;
			_addChoice = (delegate* unmanaged[Cdecl]<uint, byte*, nint*, int, int, byte*, int>)addChoice;
			_getItemType = (delegate* unmanaged[Cdecl]<uint, int, int>)getItemType;
			_setItemValue = (delegate* unmanaged[Cdecl]<uint, int, int, void>)setItemValue;
			_getItemValue = (delegate* unmanaged[Cdecl]<uint, int, int>)getItemValue;
			_setChangeCallback = (delegate* unmanaged[Cdecl]<uint, nint, void*, void>)setChangeCallback;
			_addSection = (delegate* unmanaged[Cdecl]<uint, byte*, int>)addSection;
			_getItemSection = (delegate* unmanaged[Cdecl]<uint, int, int>)getItemSection;
			_setMenuLayout = (delegate* unmanaged[Cdecl]<uint, int, void>)setMenuLayout;
			_getMenuLayout = (delegate* unmanaged[Cdecl]<uint, int>)getMenuLayout;
			_setItemImage = (delegate* unmanaged[Cdecl]<uint, int, byte*, void>)setItemImage;
			_getItemImage = (delegate* unmanaged[Cdecl]<uint, int, byte*, int, int>)getItemImage;
			_setItemSubtext = (delegate* unmanaged[Cdecl]<uint, int, byte*, void>)setItemSubtext;
			_getItemSubtext = (delegate* unmanaged[Cdecl]<uint, int, byte*, int, int>)getItemSubtext;
		}

		// Optional as well, newer than the first 004 exports.
		if (Supports004
			&& TryGet(lib, "cs2m_set_menu_tile_size", out nint setMenuTileSize)
			&& TryGet(lib, "cs2m_get_menu_tile_size", out nint getMenuTileSize)
			&& TryGet(lib, "cs2m_set_menu_image", out nint setMenuImage)
			&& TryGet(lib, "cs2m_get_menu_image", out nint getMenuImage)
			&& TryGet(lib, "cs2m_set_menu_pinned_item", out nint setMenuPinnedItem)
			&& TryGet(lib, "cs2m_get_menu_pinned_item", out nint getMenuPinnedItem)
			&& TryGet(lib, "cs2m_push", out nint push)
			&& TryGet(lib, "cs2m_replace", out nint replace)
			&& TryGet(lib, "cs2m_step_back", out nint stepBack)
			&& TryGet(lib, "cs2m_set_refresh_callback", out nint setRefreshCallback)
			&& TryGet(lib, "cs2m_suspend", out nint suspend)
			&& TryGet(lib, "cs2m_resume", out nint resume))
		{
			SupportsHistory = true;
			_setMenuTileSize = (delegate* unmanaged[Cdecl]<uint, int, void>)setMenuTileSize;
			_getMenuTileSize = (delegate* unmanaged[Cdecl]<uint, int>)getMenuTileSize;
			_setMenuImage = (delegate* unmanaged[Cdecl]<uint, byte*, void>)setMenuImage;
			_getMenuImage = (delegate* unmanaged[Cdecl]<uint, byte*, int, int>)getMenuImage;
			_setMenuPinnedItem = (delegate* unmanaged[Cdecl]<uint, int, void>)setMenuPinnedItem;
			_getMenuPinnedItem = (delegate* unmanaged[Cdecl]<uint, int>)getMenuPinnedItem;
			_push = (delegate* unmanaged[Cdecl]<uint, int, float, int>)push;
			_replace = (delegate* unmanaged[Cdecl]<uint, int, float, int>)replace;
			_stepBack = (delegate* unmanaged[Cdecl]<int, int, int>)stepBack;
			_setRefreshCallback = (delegate* unmanaged[Cdecl]<uint, nint, void*, void>)setRefreshCallback;
			_suspend = (delegate* unmanaged[Cdecl]<int, void>)suspend;
			_resume = (delegate* unmanaged[Cdecl]<int, void>)resume;
		}

		Loaded = true;
		return true;
	}

	private static nint Get(nint lib, string name) => NativeLibrary.GetExport(lib, name);

	private static bool TryGet(nint lib, string name, out nint address) => NativeLibrary.TryGetExport(lib, name, out address);

	private static void Require004()
	{
		if (!Supports004)
		{
			throw new NotSupportedException("The loaded cs2menus predates value items, sections and grids (ICS2Menus004). Update cs2menus.");
		}
	}

	private static void RequireHistory()
	{
		if (!SupportsHistory)
		{
			throw new NotSupportedException("The loaded cs2menus predates showcase layouts, menu history and pausing. Update cs2menus.");
		}
	}

	// --- Handshake ---
	public static int Available() => _available();

	// --- Lifetime ---
	public static uint Create(int type, ReadOnlySpan<char> title, nint onSelect, void* user)
	{
		fixed (byte* t = Utf8(title))
		{
			return _create(type, t, onSelect, user);
		}
	}

	public static void Destroy(uint menu) => _destroy(menu);
	public static bool IsValid(uint menu) => _isValid(menu) != 0;

	// --- Menu properties ---
	public static void SetTitle(uint menu, ReadOnlySpan<char> title)
	{
		fixed (byte* t = Utf8(title)) _setTitle(menu, t);
	}

	public static string GetTitle(uint menu) => ReadString(_getTitle, menu);
	public static int GetMenuType(uint menu) => _getMenuType(menu);
	public static void SetExitButton(uint menu, bool v) => _setExitButton(menu, v ? 1 : 0);
	public static bool GetExitButton(uint menu) => _getExitButton(menu) != 0;
	public static void SetCloseOnSelect(uint menu, bool v) => _setCloseOnSelect(menu, v ? 1 : 0);
	public static bool GetCloseOnSelect(uint menu) => _getCloseOnSelect(menu) != 0;
	public static void SetExitItem(uint menu, bool v) => _setExitItem(menu, v ? 1 : 0);
	public static bool GetExitItem(uint menu) => _getExitItem(menu) != 0;
	public static void SetForceType(uint menu, bool force) => _setForceType(menu, force ? 1 : 0);
	public static bool GetForceType(uint menu) => _getForceType(menu) != 0;
	public static void SetStartItem(uint menu, int item) => _setStartItem(menu, item);
	public static int GetStartItem(uint menu) => _getStartItem(menu);
	public static void SetEndCallback(uint menu, nint onEnd, void* user) => _setEndCallback(menu, onEnd, user);
	public static void SetMenuKey(uint menu, int action, int button) => _setMenuKey(menu, action, button);
	public static int GetMenuKey(uint menu, int action) => _getMenuKey(menu, action);

	public static void SetMenuLabel(uint menu, int label, ReadOnlySpan<char> text)
	{
		fixed (byte* t = Utf8(text)) _setMenuLabel(menu, label, t);
	}

	public static string GetMenuLabel(uint menu, int label) => ReadString(_getMenuLabel, menu, label);

	public static void SetMenuStyle(uint menu, int field, ReadOnlySpan<char> value)
	{
		fixed (byte* v = Utf8(value)) _setMenuStyle(menu, field, v);
	}

	public static string GetMenuStyle(uint menu, int field) => ReadString(_getMenuStyle, menu, field);

	// --- Items ---
	public static int AddItem(uint menu, ReadOnlySpan<char> text, ReadOnlySpan<char> info, bool disabled)
	{
		fixed (byte* t = Utf8(text))
		fixed (byte* i = Utf8(info))
		{
			return _addItem(menu, t, i, disabled ? 1 : 0);
		}
	}

	public static int InsertItem(uint menu, int pos, ReadOnlySpan<char> text, ReadOnlySpan<char> info, bool disabled)
	{
		fixed (byte* t = Utf8(text))
		fixed (byte* i = Utf8(info))
		{
			return _insertItem(menu, pos, t, i, disabled ? 1 : 0);
		}
	}

	public static int AddSubmenu(uint parent, ReadOnlySpan<char> text, uint child, ReadOnlySpan<char> info)
	{
		fixed (byte* t = Utf8(text))
		fixed (byte* i = Utf8(info))
		{
			return _addSubmenu(parent, t, child, i);
		}
	}

	public static void RemoveItem(uint menu, int item) => _removeItem(menu, item);
	public static void RemoveAllItems(uint menu) => _removeAllItems(menu);
	public static int ItemCount(uint menu) => _itemCount(menu);

	public static void SetItemText(uint menu, int item, ReadOnlySpan<char> text)
	{
		fixed (byte* t = Utf8(text)) _setItemText(menu, item, t);
	}

	public static string GetItemText(uint menu, int item) => ReadString(_getItemText, menu, item);

	public static void SetItemInfo(uint menu, int item, ReadOnlySpan<char> info)
	{
		fixed (byte* i = Utf8(info)) _setItemInfo(menu, item, i);
	}

	public static string GetItemInfo(uint menu, int item) => ReadString(_getItemInfo, menu, item);

	public static void SetItemDisabled(uint menu, int item, bool v) => _setItemDisabled(menu, item, v ? 1 : 0);
	public static bool GetItemDisabled(uint menu, int item) => _getItemDisabled(menu, item) != 0;
	public static void SetItemRaw(uint menu, int item, bool v) => _setItemRaw(menu, item, v ? 1 : 0);
	public static bool GetItemRaw(uint menu, int item) => _getItemRaw(menu, item) != 0;

	public static void SetItemIcon(uint menu, int item, ReadOnlySpan<char> url)
	{
		fixed (byte* u = Utf8(url)) _setItemIcon(menu, item, u);
	}

	public static string GetItemIcon(uint menu, int item) => ReadString(_getItemIcon, menu, item);

	public static void SetItemSubmenu(uint menu, int item, uint child) => _setItemSubmenu(menu, item, child);
	public static uint GetItemSubmenu(uint menu, int item) => _getItemSubmenu(menu, item);

	// --- Display ---
	public static bool Display(uint menu, int slot, float duration) => _display(menu, slot, duration) != 0;
	public static void DisplayToAll(uint menu, float duration) => _displayToAll(menu, duration);
	public static void Cancel(int slot) => _cancel(slot);
	public static bool HasMenu(int slot) => _hasMenu(slot) != 0;
	public static uint GetActiveMenu(int slot) => _getActiveMenu(slot);
	public static int GetActiveType(int slot) => _getActiveType(slot);
	public static int GetSelectedItem(int slot) => _getSelectedItem(slot);

	// --- Host coordination ---
	public static void SetExternalBusy(int slot, bool busy) => _setExternalBusy(slot, busy ? 1 : 0);
	public static bool GetExternalBusy(int slot) => _getExternalBusy(slot) != 0;

	// --- Value items ---
	public static int AddToggle(uint menu, ReadOnlySpan<char> text, bool on, ReadOnlySpan<char> info)
	{
		Require004();
		fixed (byte* t = Utf8(text))
		fixed (byte* i = Utf8(info))
		{
			return _addToggle(menu, t, on ? 1 : 0, i);
		}
	}

	public static int AddStepper(uint menu, ReadOnlySpan<char> text, int value, int min, int max, int step, ReadOnlySpan<char> info)
	{
		Require004();
		fixed (byte* t = Utf8(text))
		fixed (byte* i = Utf8(info))
		{
			return _addStepper(menu, t, value, min, max, step, i);
		}
	}

	public static int AddChoice(uint menu, ReadOnlySpan<char> text, IReadOnlyList<string> options, int selected, ReadOnlySpan<char> info)
	{
		Require004();
		// cs2menus copies the strings, so they only need to outlive the call.
		nint[] pointers = new nint[options.Count];
		try
		{
			for (int o = 0; o < options.Count; o++)
			{
				pointers[o] = Marshal.StringToCoTaskMemUTF8(options[o] ?? string.Empty);
			}
			fixed (byte* t = Utf8(text))
			fixed (byte* i = Utf8(info))
			fixed (nint* p = pointers)
			{
				return _addChoice(menu, t, p, pointers.Length, selected, i);
			}
		}
		finally
		{
			foreach (nint pointer in pointers)
			{
				Marshal.FreeCoTaskMem(pointer);
			}
		}
	}

	public static int GetItemType(uint menu, int item) => Supports004 ? _getItemType(menu, item) : 0;

	public static void SetItemValue(uint menu, int item, int value)
	{
		Require004();
		_setItemValue(menu, item, value);
	}

	public static int GetItemValue(uint menu, int item) => Supports004 ? _getItemValue(menu, item) : 0;

	// No-op on an older cs2menus, where no item can change.
	public static void SetChangeCallback(uint menu, nint onChange, void* user)
	{
		if (Supports004)
		{
			_setChangeCallback(menu, onChange, user);
		}
	}

	// --- Sections and grids ---
	public static int AddSection(uint menu, ReadOnlySpan<char> name)
	{
		Require004();
		fixed (byte* n = Utf8(name))
		{
			return _addSection(menu, n);
		}
	}

	public static int GetItemSection(uint menu, int item) => Supports004 ? _getItemSection(menu, item) : -1;

	public static void SetMenuLayout(uint menu, int layout)
	{
		Require004();
		_setMenuLayout(menu, layout);
	}

	public static int GetMenuLayout(uint menu) => Supports004 ? _getMenuLayout(menu) : 0;

	public static void SetItemImage(uint menu, int item, ReadOnlySpan<char> image)
	{
		Require004();
		fixed (byte* i = Utf8(image)) _setItemImage(menu, item, i);
	}

	public static string GetItemImage(uint menu, int item) => Supports004 ? ReadString(_getItemImage, menu, item) : string.Empty;

	public static void SetItemSubtext(uint menu, int item, ReadOnlySpan<char> subtext)
	{
		Require004();
		fixed (byte* s = Utf8(subtext)) _setItemSubtext(menu, item, s);
	}

	public static string GetItemSubtext(uint menu, int item) => Supports004 ? ReadString(_getItemSubtext, menu, item) : string.Empty;

	// --- Panorama layouts ---
	public static void SetMenuTileSize(uint menu, int size)
	{
		RequireHistory();
		_setMenuTileSize(menu, size);
	}

	public static int GetMenuTileSize(uint menu) => SupportsHistory ? _getMenuTileSize(menu) : 0;

	public static void SetMenuImage(uint menu, ReadOnlySpan<char> image)
	{
		RequireHistory();
		fixed (byte* i = Utf8(image)) _setMenuImage(menu, i);
	}

	public static string GetMenuImage(uint menu) => SupportsHistory ? ReadString(_getMenuImage, menu) : string.Empty;

	public static void SetMenuPinnedItem(uint menu, int item)
	{
		RequireHistory();
		_setMenuPinnedItem(menu, item);
	}

	public static int GetMenuPinnedItem(uint menu) => SupportsHistory ? _getMenuPinnedItem(menu) : -1;

	// --- History ---
	public static bool Push(uint menu, int slot, float duration)
	{
		RequireHistory();
		return _push(menu, slot, duration) != 0;
	}

	public static bool Replace(uint menu, int slot, float duration)
	{
		RequireHistory();
		return _replace(menu, slot, duration) != 0;
	}

	public static bool StepBack(int slot, int steps)
	{
		RequireHistory();
		return _stepBack(slot, steps) != 0;
	}

	// No-op on an older cs2menus, which has no refresh button.
	public static void SetRefreshCallback(uint menu, nint onRefresh, void* user)
	{
		if (SupportsHistory)
		{
			_setRefreshCallback(menu, onRefresh, user);
		}
	}

	// --- Pausing a display ---
	public static void Suspend(int slot)
	{
		RequireHistory();
		_suspend(slot);
	}

	public static void Resume(int slot)
	{
		RequireHistory();
		_resume(slot);
	}

	private static string ReadString(delegate* unmanaged[Cdecl]<uint, int, byte*, int, int> fn, uint menu, int item)
	{
		int needed = fn(menu, item, null, 0); // includes NUL
		if (needed <= 1)
		{
			return string.Empty;
		}
		byte[] buf = new byte[needed];
		fixed (byte* p = buf)
		{
			fn(menu, item, p, needed);
			return Marshal.PtrToStringUTF8((nint)p) ?? string.Empty;
		}
	}

	// Same as above for exports that key on the menu handle alone (e.g. cs2m_get_title).
	private static string ReadString(delegate* unmanaged[Cdecl]<uint, byte*, int, int> fn, uint menu)
	{
		int needed = fn(menu, null, 0);
		if (needed <= 1)
		{
			return string.Empty;
		}
		byte[] buf = new byte[needed];
		fixed (byte* p = buf)
		{
			fn(menu, p, needed);
			return Marshal.PtrToStringUTF8((nint)p) ?? string.Empty;
		}
	}

	// NUL-terminated UTF-8 bytes for fixed/pinning.
	private static byte[] Utf8(ReadOnlySpan<char> s)
	{
		int len = System.Text.Encoding.UTF8.GetByteCount(s);
		byte[] b = new byte[len + 1];
		System.Text.Encoding.UTF8.GetBytes(s, b);
		b[len] = 0;
		return b;
	}
}
