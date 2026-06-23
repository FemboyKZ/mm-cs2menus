# cs2menus bridges (managed hosts)

cs2menus exposes a C++ interface (`ICS2Menus002`) that only sibling Metamod plugins built with the same toolchain can consume.

Managed frameworks (SwiftlyS2, CounterStrikeSharp) run a .NET runtime inside the game process and can only call a flat C ABI.

The bridge is two layers:

1. **C ABI facade** - `src/public/cs2menus_capi.h` + `src/bridge/capi.cpp`, compiled into the cs2menus plugin.
   Flattens the vtable to `extern "C"` exports,
   swaps `std::function` for C function pointers + an opaque user token,
   copies strings into caller buffers. Framework-agnostic:
   the **same exports** serve any in-process .NET host.

2. **Per-framework wrapper** - thin managed package that P/Invokes the facade,
   wraps it in an idiomatic API, and (critically) destroys every menu it created on plugin unload.

Layout:

```text
bridges/
  src/                                               framework-agnostic core (shared)
    Cs2MenusNative.cs                                 P/Invoke bindings (namespace Cs2Menus)
    Cs2Menus.cs                                       Cs2MenusBridge / Cs2Menu / enums / trampolines
  swiftly/SwiftlyS2.Cs2Menus/                        SwiftlyS2 package: LoadDefault(ISwiftlyCore)
  counterstrikesharp/CounterStrikeSharp.Cs2Menus/    CS# package: LoadDefault() via Server.GameDirectory
```

Both packages link the same `src/*.cs` and add only a `Cs2MenusBridge.*.cs` partial with their host's path resolution.

Public namespace is `Cs2Menus` for both.

## Install

Each release ships a per-framework zip.
Extract it into your `game/csgo` directory.
cs2menus itself (the Metamod plugin) must already be installed.

**SwiftlyS2** - the dll installs as a Swiftly _export_:

```text
addons/swiftlys2/plugins/SwiftlyS2.Cs2Menus/resources/exports/SwiftlyS2.Cs2Menus.dll
```

**CounterStrikeSharp** - the dll installs as a shared API:

```text
addons/counterstrikesharp/shared/CounterStrikeSharp.Cs2Menus/CounterStrikeSharp.Cs2Menus.dll
```

In your own plugin, reference the dll for compile only.

Do **not** copy it into your plugin folder,
the shared install above provides it at runtime:

```xml
<Reference Include="SwiftlyS2.Cs2Menus">
  <HintPath>path/to/SwiftlyS2.Cs2Menus.dll</HintPath>
  <Private>false</Private>
</Reference>
```

## SwiftlyS2 usage

```csharp
using Cs2Menus;

// In plugin load - resolves cs2menus from <csgo>/addons/cs2menus/bin/<platform>/ for you:
Cs2MenusBridge.LoadDefault(Core); // Core = ISwiftlyCore
var menus = new Cs2MenusBridge();
// (or Cs2MenusBridge.Load(absolutePath) if cs2menus lives somewhere custom)

if (menus.Available)
{
    var m = menus.CreateMenu(MenuType.Default, "\x04Pick a map",
        (menu, slot, item) =>
        {
            string map = menu.GetItemInfo(item);
            // act on selection
        });

    m.AddItem("Dust II", "de_dust2")
     .AddItem("Mirage", "de_mirage")
     .AddItem("Coming soon", disabled: true);

    m.Display(slot, 20f);
}

// In plugin unload - MANDATORY:
menus.Dispose(); // destroys every menu, frees callback tokens
```

## Lifecycle - the one rule that matters

cs2menus holds the native callback pointers for a menu until you destroy it.
If your managed assembly unloads (hot-reload) while a menu still exists,
cs2menus calls freed pointers and the server crashes.

Own a single `Cs2MenusBridge` per plugin and `Dispose()` it on unload.
It tracks every handle it created and destroys them.
Individual `Cs2Menu.Dispose()` works too for one-shot menus (e.g. from the `Ended` handler).

Threading is not a concern: cs2menus is fully thread-safe and callbacks always arrive on the game main thread, so no frame marshaling is needed.

## CounterStrikeSharp usage

Same core, same API. Only path resolution + cleanup wiring differ.

```csharp
using Cs2Menus;
using CounterStrikeSharp.API.Core;

public class MyPlugin : BasePlugin
{
    private Cs2MenusBridge _menus = null!;

    public override void Load(bool hotReload)
    {
        // Resolves cs2menus from Server.GameDirectory/addons/cs2menus/bin/<platform>/.
        Cs2MenusBridge.LoadDefault();
        _menus = new Cs2MenusBridge();

        if (_menus.Available)
        {
            var m = _menus.CreateMenu(MenuType.Default, "\x04Pick a map",
                (menu, slot, item) => { string map = menu.GetItemInfo(item); });
            m.AddItem("Dust II", "de_dust2").AddItem("Mirage", "de_mirage");
            // map a controller to its slot: player.Slot
            m.Display(player.Slot, 20f);
        }
    }

    // MANDATORY: also fires on hot reload (DLL swap), before the assembly unloads.
    public override void Unload(bool hotReload) => _menus.Dispose();
}
```

Notes:

- `LoadDefault()` takes no args - CS#'s `Server.GameDirectory` is static.
- Cleanup goes in `Unload(bool)`, which CS# calls on hot reload too,
  exactly when stale callback pointers would otherwise crash the server.
- Map a `CCSPlayerController` to the facade's `int slot` via `controller.Slot`.

The facade (`cs2menus_capi.h`) and `src/*.cs` are identical to the SwiftlyS2 path.

## Input ownership

cs2menus, SwiftlyS2's own menus, and CS#'s own menus each claim chat `say` input,
per-frame button polling, and the same center-HTML channel (`show_survival_respawn_status`).
Two systems open for one player at once fight over these.

### cs2menus yields to the host

Tell cs2menus when a host menu owns a slot. While marked busy, cs2menus cancels
any menu on that slot and refuses new displays, so it stays out of the host's way.
cs2menus never auto-reopens when released, the host owns that.

**SwiftlyS2** - one call, wired to the host's menu events automatically:

```csharp
Cs2MenusBridge.TrackHostMenus(Core); // Core = ISwiftlyCore
```

**CounterStrikeSharp** - CS#'s `MenuManager` is static and exposes no open/close events,
so wire it yourself where you open/close menus:

```csharp
MenuManager.OpenCenterHtmlMenu(this, player, menu);
Cs2MenusBridge.SetHostMenuBusy(player.Slot, true);
// ... when the menu closes:
Cs2MenusBridge.SetHostMenuBusy(player.Slot, false);
```

### The host yields to cs2menus

The other direction already works via introspection: before opening your own
menu, check whether cs2menus has the slot.

```csharp
if (Cs2MenusBridge.GetActiveType(slot) != MenuType.Html)
{
   /* center-HTML channel is free */
}
```

The raw exports `cs2m_get_active_type` / `cs2m_has_menu` back these.
