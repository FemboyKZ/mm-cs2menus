# CS2 Menu API

![Downloads](https://img.shields.io/github/downloads/FemboyKZ/mm-cs2menus/total?style=flat-square) ![Last commit](https://img.shields.io/github/last-commit/FemboyKZ/mm-cs2menus?style=flat-square) ![Open issues](https://img.shields.io/github/issues/FemboyKZ/mm-cs2menus?style=flat-square) ![Closed issues](https://img.shields.io/github/issues-closed/FemboyKZ/mm-cs2menus?style=flat-square) ![Size](https://img.shields.io/github/repo-size/FemboyKZ/mm-cs2menus?style=flat-square) ![GitHub Workflow Status](https://img.shields.io/github/actions/workflow/status/FemboyKZ/mm-cs2menus/build.yml?style=flat-square)

A Metamod:Source plugin that provides a shared **menu API** for CS2.
It owns player input and rendering so other Metamod plugins can create interactive menus without each one reimplementing chat parsing, HTML rendering, and pagination.

## Menu types

| Type     | Render                        | Navigation                                                           | Notes                                                                             |
| -------- | ----------------------------- | -------------------------------------------------------------------- | --------------------------------------------------------------------------------- |
| **Chat** | Numbered list printed to chat | Type an item number, `0` exits, page keys follow items               | Always available                                                                  |
| **HTML** | Center-screen panel           | Movement keys - W/S move, D select, A exit by default (configurable) | Requires runtime signature + schema (see [HTML availability](#html-availability)) |

Pass `MenuType::Default` to use the server's configured default style.

## For server owners

1. Install [Metamod:Source 2.0](https://www.metamodsource.net/downloads.php?branch=dev) on a CS2 dedicated server.
2. Drop the built plugin into `addons/` and the config into `cfg/cs2menus/core.cfg`.
3. On its own, this plugin does nothing visible, it's a library other plugins use.

## Configuration

`cfg/cs2menus/core.cfg`, reloaded automatically on every map change.

### `General`

| Key                   | Default | Meaning                                                                             |
| --------------------- | ------- | ----------------------------------------------------------------------------------- |
| `CommandPrefix`       | `!`     | Chars a player may type before a chat-menu number (`!1`). Bare numbers always work. |
| `SilentCommandPrefix` | `/`     | As above, both sets are accepted for menu input.                                    |

### `Menu`

| Key                                           | Default               | Meaning                                                                                                                                                                                                   |
| --------------------------------------------- | --------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `DefaultType`                                 | `chat`                | Style for `MenuType::Default` menus: `chat` or `html`.                                                                                                                                                    |
| `ItemsPerPage`                                | `6`                   | Chat items per page (clamped 1-7). Items number from `1`. Next page is item count + 1, Prev is + 2, `0` exits.                                                                                            |
| `ExitButton`                                  | `1`                   | Default state of the exit entry on new menus (consumers can override per-menu).                                                                                                                           |
| `HtmlVisibleItems`                            | `7`                   | Rows visible at once in an HTML menu (clamped 1-7).                                                                                                                                                       |
| `HtmlExitItem`                                | `0`                   | Default state of the selectable "Exit" row on HTML menus. Auto-forced on when `NavBack` is `none` so a menu is never stuck.                                                                               |
| `HtmlNavColor`                                | `#ff2ee7`             | Highlighted/selected row + cursor color.                                                                                                                                                                  |
| `HtmlFooterColor`                             | `#909090`             | Footer key-hint color.                                                                                                                                                                                    |
| `HtmlDisabledColor`                           | `#808080`             | Greyed-out item color.                                                                                                                                                                                    |
| `NavUp` / `NavDown` / `NavSelect` / `NavBack` | `w` / `s` / `d` / `a` | HTML nav keys. Valid: `w a s d`, `e`/`use`, `shift`/`speed`, `ctrl`/`duck`, `space`/`jump`, `r`/`reload`, `mouse1`, `mouse2`, `tab`, `f`/`inspect`, or `none` to disable. Unknown names keep the default. |
| `HtmlFixFlashing`                             | `0`                   | Workaround for the HTML panel flashing (fakes `CCSGameRules::m_bGameRestart` while a menu is open). **Breaks warmup UI while a menu is shown** - off by default.                                          |

## For plugin developers

Acquire the interface via Metamod's factory (interface name `ICS2Menus002`):

```cpp
#include "ics2menus.h"

ICS2Menus *g_pMenus = nullptr;

void AcquireMenus() {
    g_pMenus = (ICS2Menus *)g_SMAPI->MetaFactory(CS2MENUS_INTERFACE, nullptr, nullptr);
}
```

Re-resolve it from `IMetamodListener::OnPluginLoad` / `OnPluginUnload` so you never hold a dangling pointer.

### Example

```cpp
MenuHandle m = g_pMenus->CreateMenu(MenuType::Default, "\x04Pick a map",
    [](MenuHandle menu, int slot, int item) {
        const char *info = g_pMenus->GetItemInfo(menu, item); // e.g. "de_dust2"
        // ... act on the selection
    });

g_pMenus->AddItem(m, "Dust II", "de_dust2", false);
g_pMenus->AddItem(m, "Mirage",  "de_mirage", false);
g_pMenus->AddItem(m, "Coming soon", "", /*disabled*/ true);

// One-shot: free the menu when the player's display ends.
g_pMenus->SetMenuEndCallback(m, [](MenuHandle menu, int slot, MenuEndReason reason) {
    g_pMenus->DestroyMenu(menu);
});

// HTML-only: override nav keys for this menu (no-op for chat menus).
g_pMenus->SetMenuKey(m, MenuNavAction::Select, MenuButton::Use);   // E selects
g_pMenus->SetMenuKey(m, MenuNavAction::Back,   MenuButton::Speed); // Shift backs out

g_pMenus->DisplayMenu(m, slot, 20.0f); // show to one player, 20s timeout (0 = none)
```

Beyond the basics shown above, the API also covers:

- **Submenus** - `AddSubMenu(parent, text, child, info)` adds an item that opens another menu. The Back key returns to the parent.
- **Display to everyone** - `DisplayMenuToAll(menu, duration)`.
- **Live mutation** - `SetItemText` / `SetItemInfo` / `SetItemDisabled` / `RemoveItem` / `RemoveAllItems` re-render any active viewers in place.
- **Introspection** - `GetItemCount`, `GetSelectedItem`, `HasMenu`, `GetActiveMenu`, `GetActiveMenuType`.
- **Per-menu behavior** - `SetCloseOnSelect`, `SetStartItem`, `SetExitItem`.
- **Host coordination** - `SetExternalBusy(slot, busy)` makes cs2menus yield a slot to another menu system (e.g. SwiftlyS2 / CS#) so they never fight for input or the center-HTML channel. `GetExternalBusy(slot)` reads it back.

The full surface is documented in [`ics2menus.h`](src/public/ics2menus.h).

Managed plugins (SwiftlyS2, CounterStrikeSharp) consume this via the flat C ABI bridge in [`bridges/`](bridges/).

### Contracts - read these

- **Thread-safe.** Every method is callable from the main (game) thread or a worker thread.
  Build/query calls (`CreateMenu`, `AddItem`, `SetX`, `GetX`...) run inline under a lock.
  `DisplayMenu` / `CancelMenu` off the main thread are queued for the next `GameFrame`,
  and `DisplayMenu` then returns `true` optimistically.
  `onSelect` / `onEnd` callbacks always fire on the main thread.
  An off-thread `DestroyMenu` invalidates the handle at once but skips the `Destroyed` callback.
  Don't block a main-thread callback on a worker that re-enters this API. The lock is held, so you deadlock.
- **Pointers alias internal storage.** `GetItemText` / `GetItemInfo` return pointers into live menu state.
  Copy them, don't cache them across calls.
- **Destroy what you create.** A menu's callbacks may capture pointers into _your_ plugin.
  Call `DestroyMenu` for every menu you create (and `CancelMenu` for open displays) in your plugin's `Unload()`,
  so this plugin never invokes a lambda inside an unloaded DLL.
  `cs2menus`'s own `Unload()` drops all menus without firing callbacks.
- **Same toolchain.** `std::function` / `std::string` / `const char*` cross the interface boundary.
  A consumer built with a different toolchain is undefined behavior.

### HTML availability

HTML menus need two runtime lookups to succeed:

1. The `GameEventManager` **signature** (to send the center-screen event).
2. The pawn **button schema** (`m_pMovementServices` -> `m_nButtons` -> `m_pButtonStates`, to read navigation input).

If either fails, the plugin **automatically downgrades HTML menus to chat** and logs `HTML menus unavailable (falling back to chat)`.
Consumers don't need to handle this.
A `CreateMenu(MenuType::Html, ...)` simply renders as a chat menu instead.
Re-checked on every map change.

### SwiftlyS2 and CounterStrikeSharp

CS2Menus supports both platforms via [bridges](/bridges/README.md).

## Build

### Prerequisites

- This repository is cloned recursively (ie. has submodules)
- [python3](https://www.python.org/)
- [ambuild](https://github.com/alliedmodders/ambuild), make sure `ambuild` command is available via the `PATH` environment variable;
- MSVC (VS build tools)/Clang installed for Windows/Linux.

### AMBuild

```bash
mkdir -p build && cd build
python3 ../configure.py --enable-optimize
ambuild
```

## Credits

- [zer0.k's MetaMod Sample plugin fork](https://github.com/zer0k-z/mm_misc_plugins)
- [cs2kz-metamod](https://github.com/KZGlobalTeam/cs2kz-metamod) - gamedata signatures, button-state schema
- [CS2Fixes](https://github.com/Source2ZE/CS2Fixes) - center-HTML (`show_survival_respawn_status`) technique, gamedata
- [SwiftlyS2](https://github.com/swiftly-solution/swiftlys2) - HTML menu design reference
