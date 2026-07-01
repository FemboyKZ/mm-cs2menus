# CS2 Menu API

![Downloads](https://img.shields.io/github/downloads/FemboyKZ/mm-cs2menus/total?style=flat-square) ![Last commit](https://img.shields.io/github/last-commit/FemboyKZ/mm-cs2menus?style=flat-square) ![Open issues](https://img.shields.io/github/issues/FemboyKZ/mm-cs2menus?style=flat-square) ![Closed issues](https://img.shields.io/github/issues-closed/FemboyKZ/mm-cs2menus?style=flat-square) ![Size](https://img.shields.io/github/repo-size/FemboyKZ/mm-cs2menus?style=flat-square) ![GitHub Workflow Status](https://img.shields.io/github/actions/workflow/status/FemboyKZ/mm-cs2menus/build.yml?style=flat-square)

A Metamod:Source plugin that provides a shared **menu API** for CS2.

It owns player input and rendering so other Metamod plugins can create interactive menus without each one reimplementing chat parsing, HTML rendering, and pagination.

## Menu types

| Type     | Render                        | Navigation                                                           |
| -------- | ----------------------------- | -------------------------------------------------------------------- |
| **Chat** | Numbered list printed to chat | Type an item number, `0` exits, page keys follow items               |
| **HTML** | Center-screen panel           | Movement keys - W/S move, D select, A exit by default (configurable) |

Pass `MenuType::Default` to use the server's configured default style.

For commands see: [COMMANDS.md](./COMMANDS.md).

## Plugins using this library

- [CS2RockTheVote](https://github.com/FemboyKZ/mm-cs2rockthevote)
- [CS2KZ-Metamod](https://github.com/KZGlobalTeam/cs2kz-metamod)

## For server owners

### Dependencies

- CS2 Server
- [Metamod:Source 2.0](https://www.metamodsource.net/downloads.php?branch=dev)
- (Optional) [ClientConvarValue](https://github.com/komashchenko/ClientCvarValue) - For client translation support
- (Optional) [sql_mm](https://github.com/zer0k-z/sql_mm) - For per-player menu preferences (see below)
- (Optional) [MM-CS2Admin](https://github.com/FemboyKZ/mm-cs2admin) - For command overrides (gate commands to admins only).

### Installation

1. Download the [latest release](https://github.com/FemboyKZ/mm-cs2menus/releases/latest).
2. Extract the downloaded `.zip` file into your server's root folder (`~/game/csgo/`).
3. (Optional) If you use [CounterStrikeSharp](https://github.com/roflmuffin/CounterStrikeSharp) and/or [SwiftlyS2](https://github.com/swiftly-solution/swiftlys2), you might want to install the **bridge** plugins.

> [!note]
> On its own, this plugin does nothing visible, it's a library other plugins use.

### Configuration

`cfg/cs2menus/core.cfg`, reloaded automatically on every map change.

TBA...

## For plugin developers

Acquire the interface via Metamod's factory (interface name `ICS2Menus003`):

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

The full surface is documented in [`ics2menus.h`](src/public/ics2menus.h).

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
