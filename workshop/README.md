# cs2menus workshop addon

Panorama layouts for cs2menus' `panorama` menu type: `menu.xml` for lists and `grid.xml` for image tiles.
The plugin spawns one `custom_hud_layout` per player and layout, pointing at `panorama/layout/custom_game/cs2menus/<layout>.vxml_c`.
It falls back to HTML menus when `menu.vxml_c` isn't mounted, and to the list when `grid.vxml_c` isn't.

Derived from cs2kz's menu layout ([cs2kz-metamod](https://github.com/KZGlobalTeam/cs2kz-metamod) `workshop/panorama`, by zer0.k).
Unlike the rest of this repository, everything in this folder is licensed under the GNU AGPL v3, see [LICENSE](LICENSE).

## Contract with the plugin

Panel ids, dialog variables and classes are written by `src/render/panorama_hud.cpp`. Keep both sides in step:

- Every panel id and dialog variable starts with the layout's prefix, `cm_` in `menu.xml` and `cg_` in `grid.xml`.
  The client matches them by name across all custom HUD layouts, so an unprefixed `item0` would collide with cs2kz's menu.
  After the prefix both layouts share names (`root`, `title`, `close`, `pages`, `nav<N>`, `item<N>`, `val<N>` and the popups), so one writer serves both.
- The client validates custom HUD layouts: only `Panel`, `Label` and `Button`, and a `Label` rejects `html`.
  Text is plain, colors are classes.
- Slot counts: 20 `cm_nav<N>`, 30 `cm_item<N>` with 27 `cm_seg<N>_<S>` runs each, 4 `cm_step_b<N>` and 16 `cm_li<N>`
  (`kNavSlots`, `kItemSlots`, `kRowSegments`, `kStepButtons`, `kListSlots` in `src/render/panorama_hud.h`).
- Grid: 10 `cg_nav<N>` tabs, 24 `cg_item<N>` tiles with `cg_img<N>`, `cg_name<N>` and `cg_val<N>`, and the `cg_pager` arrows
  (`kGridTabs`, `kGridSlots`). `grid.css` has an `img-<icon>` class per file in the game's `panorama/images/icons/equipment`.
- The game interns at most 1024 panel ids and dialog variables per layout. `menu.xml` sits near 970, `grid.xml` near 175. Budget any new id.
- Value items: rows get `type-toggle` (plus `on`), `type-step` or `type-choice`.
  Editing opens `cm_step` or `cm_list` beside the menu box and adds `shift` to `cm_root` (`cg_` likewise).
- The `cm-col<N>` classes in `menu.css` match `kPalette` in `src/render/panorama_hud.cpp`.
- Font classes in `fonts.css` match `kPanoramaFonts` in `src/cs2menus.cpp`.

## Building and publishing

1. Install the CS2 Workshop Tools and create an addon, e.g. `cs2menus`.
2. Copy `panorama/` into `content/csgo_addons/cs2menus/`.
3. Open the addon in the tools so the layout and styles compile, then publish it to the Workshop.
4. Put the Workshop ID in `cfg/cs2menus/core.cfg` under `Panorama` > `WorkshopId`.
   With [MultiAddonManager](https://github.com/Source2ZE/MultiAddonManager) loaded, cs2menus mounts it on server start.
