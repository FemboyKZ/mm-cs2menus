# CS2Menus test consumers / example plugins

Minimal consumer plugins for each host, to validate the cs2menus interface and the managed bridges end to end.
Each opens a small demo menu (item + disabled row - submenu, with select/end callbacks) and exercises the `SetExternalBusy` coordination hook.

All require the cs2menus Metamod plugin installed and loaded on the server.

| Folder               | Host                    | Trigger                           |
| -------------------- | ----------------------- | --------------------------------- |
| `metamod`            | Metamod (C++)           | chat `!cs2menu`, `!cs2busy`       |
| `swiftlys2`          | SwiftlyS2 (C#)          | `css_cs2menu`, `css_cs2menu_busy` |
| `counterstrikesharp` | CounterStrikeSharp (C#) | `css_cs2menu`, `css_cs2menu_busy` |

## metamod (native)

Uses `ICS2Menus002` directly via `MetaFactory`.
Built as an opt-in extra target that reuses the cs2menus SDK setup.

bash:

```bash
cd build
CS2MENUS_BUILD_EXAMPLES=1 python3 ../configure.py --enable-optimize
ambuild
```

PowerShell:

```pwsh
cd build
$env:CS2MENUS_BUILD_EXAMPLES = "1"
python ../configure.py --enable-optimize
ambuild
```

The binary lands in the build tree as `cs2menus_consumer.*`.
Drop it next to a normal Metamod plugin and add a `.vdf` pointing at it.
Type `!cs2menu` in chat.

## swiftlys2 / counterstrikesharp

Both reference the matching bridge project and bundle its dll, so the test plugin is self-contained:

```bash
dotnet build examples/swiftlys2/Cs2MenusSwiftlyExample.csproj -c Release
dotnet build examples/counterstrikesharp/Cs2MenusCssExample.csproj -c Release
```

Install the build output into the host's plugin folder (see each framework's docs),
then use the commands above.
The bridge resolves the native cs2menus binary automatically via `LoadDefault`.

In production, install the bridge once to its shared location (see [`../bridges/README.md`](../bridges/README.md))
and reference it with `<Private>false</Private>` instead of bundling.
