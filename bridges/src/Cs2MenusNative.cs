using System;
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
/// </summary>
internal static unsafe class Cs2MenusNative
{
	// Handshake
	private static delegate* unmanaged[Cdecl]<int> _abiVersion;
	private static delegate* unmanaged[Cdecl]<int> _available;

	// Build
	private static delegate* unmanaged[Cdecl]<int, byte*, nint, void*, uint> _create;
	private static delegate* unmanaged[Cdecl]<uint, byte*, byte*, int, int> _addItem;
	private static delegate* unmanaged[Cdecl]<uint, byte*, void> _setTitle;
	private static delegate* unmanaged[Cdecl]<uint, int, void> _setExitButton;
	private static delegate* unmanaged[Cdecl]<uint, int, void> _setCloseOnSelect;
	private static delegate* unmanaged[Cdecl]<uint, nint, void*, void> _setEndCallback;
	private static delegate* unmanaged[Cdecl]<uint, int, void> _setExitItem;
	private static delegate* unmanaged[Cdecl]<uint, int, int, void> _setMenuKey;
	private static delegate* unmanaged[Cdecl]<uint, int, void> _setStartItem;
	private static delegate* unmanaged[Cdecl]<uint, byte*, uint, byte*, int> _addSubmenu;

	// Show / hide
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

	// Lifetime / introspection / mutation
	private static delegate* unmanaged[Cdecl]<uint, void> _destroy;
	private static delegate* unmanaged[Cdecl]<uint, int> _itemCount;
	private static delegate* unmanaged[Cdecl]<uint, int, byte*, int, int> _getItemText;
	private static delegate* unmanaged[Cdecl]<uint, int, byte*, int, int> _getItemInfo;
	private static delegate* unmanaged[Cdecl]<uint, int, byte*, void> _setItemText;
	private static delegate* unmanaged[Cdecl]<uint, int, byte*, void> _setItemInfo;
	private static delegate* unmanaged[Cdecl]<uint, int, int, void> _setItemDisabled;
	private static delegate* unmanaged[Cdecl]<uint, int, int> _getItemDisabled;
	private static delegate* unmanaged[Cdecl]<uint, int, void> _removeItem;
	private static delegate* unmanaged[Cdecl]<uint, void> _removeAllItems;
	private static delegate* unmanaged[Cdecl]<uint, int> _getStartItem;

	public static bool Loaded { get; private set; }

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
			_abiVersion = (delegate* unmanaged[Cdecl]<int>)Get(lib, "cs2m_abi_version");
			_available = (delegate* unmanaged[Cdecl]<int>)Get(lib, "cs2m_available");
			_create = (delegate* unmanaged[Cdecl]<int, byte*, nint, void*, uint>)Get(lib, "cs2m_create");
			_addItem = (delegate* unmanaged[Cdecl]<uint, byte*, byte*, int, int>)Get(lib, "cs2m_add_item");
			_setTitle = (delegate* unmanaged[Cdecl]<uint, byte*, void>)Get(lib, "cs2m_set_title");
			_setExitButton = (delegate* unmanaged[Cdecl]<uint, int, void>)Get(lib, "cs2m_set_exit_button");
			_setCloseOnSelect = (delegate* unmanaged[Cdecl]<uint, int, void>)Get(lib, "cs2m_set_close_on_select");
			_setEndCallback = (delegate* unmanaged[Cdecl]<uint, nint, void*, void>)Get(lib, "cs2m_set_end_callback");
			_setExitItem = (delegate* unmanaged[Cdecl]<uint, int, void>)Get(lib, "cs2m_set_exit_item");
			_setMenuKey = (delegate* unmanaged[Cdecl]<uint, int, int, void>)Get(lib, "cs2m_set_menu_key");
			_setStartItem = (delegate* unmanaged[Cdecl]<uint, int, void>)Get(lib, "cs2m_set_start_item");
			_addSubmenu = (delegate* unmanaged[Cdecl]<uint, byte*, uint, byte*, int>)Get(lib, "cs2m_add_submenu");
			_display = (delegate* unmanaged[Cdecl]<uint, int, float, int>)Get(lib, "cs2m_display");
			_displayToAll = (delegate* unmanaged[Cdecl]<uint, float, void>)Get(lib, "cs2m_display_to_all");
			_cancel = (delegate* unmanaged[Cdecl]<int, void>)Get(lib, "cs2m_cancel");
			_hasMenu = (delegate* unmanaged[Cdecl]<int, int>)Get(lib, "cs2m_has_menu");
			_getActiveMenu = (delegate* unmanaged[Cdecl]<int, uint>)Get(lib, "cs2m_get_active_menu");
			_getActiveType = (delegate* unmanaged[Cdecl]<int, int>)Get(lib, "cs2m_get_active_type");
			_getSelectedItem = (delegate* unmanaged[Cdecl]<int, int>)Get(lib, "cs2m_get_selected_item");
			_setExternalBusy = (delegate* unmanaged[Cdecl]<int, int, void>)Get(lib, "cs2m_set_external_busy");
			_getExternalBusy = (delegate* unmanaged[Cdecl]<int, int>)Get(lib, "cs2m_get_external_busy");
			_destroy = (delegate* unmanaged[Cdecl]<uint, void>)Get(lib, "cs2m_destroy");
			_itemCount = (delegate* unmanaged[Cdecl]<uint, int>)Get(lib, "cs2m_item_count");
			_getItemText = (delegate* unmanaged[Cdecl]<uint, int, byte*, int, int>)Get(lib, "cs2m_get_item_text");
			_getItemInfo = (delegate* unmanaged[Cdecl]<uint, int, byte*, int, int>)Get(lib, "cs2m_get_item_info");
			_setItemText = (delegate* unmanaged[Cdecl]<uint, int, byte*, void>)Get(lib, "cs2m_set_item_text");
			_setItemInfo = (delegate* unmanaged[Cdecl]<uint, int, byte*, void>)Get(lib, "cs2m_set_item_info");
			_setItemDisabled = (delegate* unmanaged[Cdecl]<uint, int, int, void>)Get(lib, "cs2m_set_item_disabled");
			_getItemDisabled = (delegate* unmanaged[Cdecl]<uint, int, int>)Get(lib, "cs2m_get_item_disabled");
			_removeItem = (delegate* unmanaged[Cdecl]<uint, int, void>)Get(lib, "cs2m_remove_item");
			_removeAllItems = (delegate* unmanaged[Cdecl]<uint, void>)Get(lib, "cs2m_remove_all_items");
			_getStartItem = (delegate* unmanaged[Cdecl]<uint, int>)Get(lib, "cs2m_get_start_item");
		}
		catch (EntryPointNotFoundException)
		{
			return false;
		}

		// Gate on ABI: refuse a cs2menus built against a newer/older facade.
		if (_abiVersion() != 1)
		{
			return false;
		}

		Loaded = true;
		return true;
	}

	private static nint Get(nint lib, string name) => NativeLibrary.GetExport(lib, name);

	public static int Available() => _available();

	public static uint Create(int type, ReadOnlySpan<char> title, nint onSelect, void* user)
	{
		fixed (byte* t = Utf8(title))
		{
			return _create(type, t, onSelect, user);
		}
	}

	public static int AddItem(uint menu, ReadOnlySpan<char> text, ReadOnlySpan<char> info, bool disabled)
	{
		fixed (byte* t = Utf8(text))
		fixed (byte* i = Utf8(info))
		{
			return _addItem(menu, t, i, disabled ? 1 : 0);
		}
	}

	public static void SetTitle(uint menu, ReadOnlySpan<char> title)
	{
		fixed (byte* t = Utf8(title)) _setTitle(menu, t);
	}

	public static void SetExitButton(uint menu, bool v) => _setExitButton(menu, v ? 1 : 0);
	public static void SetCloseOnSelect(uint menu, bool v) => _setCloseOnSelect(menu, v ? 1 : 0);
	public static void SetEndCallback(uint menu, nint onEnd, void* user) => _setEndCallback(menu, onEnd, user);
	public static void SetExitItem(uint menu, bool v) => _setExitItem(menu, v ? 1 : 0);
	public static void SetMenuKey(uint menu, int action, int button) => _setMenuKey(menu, action, button);
	public static void SetStartItem(uint menu, int item) => _setStartItem(menu, item);

	public static int AddSubmenu(uint parent, ReadOnlySpan<char> text, uint child, ReadOnlySpan<char> info)
	{
		fixed (byte* t = Utf8(text))
		fixed (byte* i = Utf8(info))
		{
			return _addSubmenu(parent, t, child, i);
		}
	}

	public static bool Display(uint menu, int slot, float duration) => _display(menu, slot, duration) != 0;
	public static void DisplayToAll(uint menu, float duration) => _displayToAll(menu, duration);
	public static void Cancel(int slot) => _cancel(slot);
	public static bool HasMenu(int slot) => _hasMenu(slot) != 0;
	public static uint GetActiveMenu(int slot) => _getActiveMenu(slot);
	public static int GetActiveType(int slot) => _getActiveType(slot);
	public static int GetSelectedItem(int slot) => _getSelectedItem(slot);
	public static void SetExternalBusy(int slot, bool busy) => _setExternalBusy(slot, busy ? 1 : 0);
	public static bool GetExternalBusy(int slot) => _getExternalBusy(slot) != 0;
	public static void Destroy(uint menu) => _destroy(menu);
	public static int ItemCount(uint menu) => _itemCount(menu);

	public static string GetItemText(uint menu, int item) => ReadString(_getItemText, menu, item);
	public static string GetItemInfo(uint menu, int item) => ReadString(_getItemInfo, menu, item);

	public static void SetItemText(uint menu, int item, ReadOnlySpan<char> text)
	{
		fixed (byte* t = Utf8(text)) _setItemText(menu, item, t);
	}

	public static void SetItemInfo(uint menu, int item, ReadOnlySpan<char> info)
	{
		fixed (byte* i = Utf8(info)) _setItemInfo(menu, item, i);
	}

	public static void SetItemDisabled(uint menu, int item, bool v) => _setItemDisabled(menu, item, v ? 1 : 0);
	public static bool GetItemDisabled(uint menu, int item) => _getItemDisabled(menu, item) != 0;
	public static void RemoveItem(uint menu, int item) => _removeItem(menu, item);
	public static void RemoveAllItems(uint menu) => _removeAllItems(menu);
	public static int GetStartItem(uint menu) => _getStartItem(menu);

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
