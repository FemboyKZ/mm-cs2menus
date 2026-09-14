# cs2menus workshop addon

Panorama layout for cs2menus' `panorama` menu type.
The plugin spawns one `custom_hud_layout` per player pointing at `panorama/layout/custom_game/cs2menus/menu.vxml_c`, and falls back to HTML menus when that file isn't mounted.

Derived from cs2kz's menu layout ([cs2kz-metamod](https://github.com/KZGlobalTeam/cs2kz-metamod) `workshop/panorama`, by zer0.k).
Unlike the rest of this repository, everything in this folder is licensed under the GNU AGPL v3, see [LICENSE](LICENSE).

## Contract with the plugin

Panel ids, dialog variables and classes are written by `src/render/panorama_hud.cpp`. Keep both sides in step:

- Every panel id and dialog variable starts with `cm_`.
  The client matches them by name across all custom HUD layouts, so an unprefixed `item0` would collide with cs2kz's menu.
- The client validates custom HUD layouts: only `Panel`, `Label` and `Button`, and a `Label` rejects `html`.
  Text is plain, colors are classes.
- Slot counts: 20 `cm_nav<N>`, 40 `cm_item<N>` with 10 `cm_seg<N>_<S>` runs each
  (`kNavSlots`, `kItemSlots`, `kRowSegments` in `src/render/panorama_hud.h`).
- The `cm-col<N>` classes in `menu.css` match `kPalette` in `src/render/panorama_hud.cpp`.
- Font classes in `fonts.css` match `kPanoramaFonts` in `src/cs2menus.cpp`.

## Building and publishing

1. Install the CS2 Workshop Tools and create an addon, e.g. `cs2menus`.
2. Copy `panorama/` into `content/csgo_addons/cs2menus/`.
3. Open the addon in the tools so the layout and styles compile, then publish it to the Workshop.
4. Put the Workshop ID in `cfg/cs2menus/core.cfg` under `Panorama` > `WorkshopId`.
   With [MultiAddonManager](https://github.com/Source2ZE/MultiAddonManager) loaded, cs2menus mounts it on server start.
