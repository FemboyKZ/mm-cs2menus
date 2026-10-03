# cs2menus workshop addon

Panorama layouts for cs2menus' `panorama` menu type: `menu.xml` for lists, `grid.xml` for image tiles, `showcase.xml` for one item's editor,
`studio.xml` for an editor beside a 3D preview and `columns.xml` for everything at a glance.
`menu.xml` is written by hand, `tools/gen_layouts.py` builds the other four: change it, not them.
The plugin spawns one `custom_hud_layout` per player and layout, pointing at `panorama/layout/custom_game/cs2menus/<layout>.vxml_c`.
It falls back to HTML menus when `menu.vxml_c` isn't mounted, and to the list when another layout isn't.

Derived from cs2kz's menu layout ([cs2kz-metamod](https://github.com/KZGlobalTeam/cs2kz-metamod) `workshop/panorama`, by zer0.k).
Unlike the rest of this repository, everything in this folder is licensed under the GNU AGPL v3, see [LICENSE](LICENSE).

## Contract with the plugin

`src/render/panorama_hud.cpp` writes the panel ids, dialog variables and classes the layouts declare. Keep both sides in step:

- **Names.** Every id and dialog variable starts with its layout's prefix: `cm_` list, `cg_` grid, `cx_` showcase, `cs_` studio,
  `cl_` columns. The client matches them by name across all custom HUD layouts, so an unprefixed `item0` would collide with
  cs2kz's menu. After the prefix the layouts share names, so one writer serves them all.
- **Slot counts** are the `k...` constants in `src/render/panorama_hud.h`. `tools/gen_layouts.py` names the one beside each
  count it builds to, `menu.xml` has them by hand.
- **What the client accepts.** A custom HUD layout may only hold `Panel`, `Label` and `Button`, and a `Label` rejects `html`.
  Text is plain, colors and states are classes.
- **Id budget.** The game interns at most 1024 panel ids and dialog variables per layout, and `menu.xml` uses 985.
  `gen_layouts.py` prints the counts, budget any new id.
- **Widths.** The server can't measure text: what fits a tab row is guessed from the pixel constants at the top of
  `src/menu/manager.cpp` (`kTabRow` and the ones beside it). Change them with the styles.
- **Palette and fonts.** `cm-col<N>` in `menu.css` match `kPalette` in `panorama_hud.cpp`, the classes in `fonts.css` match
  `kPanoramaFonts` in `src/cs2menus.cpp`.
- **Generated styles.** Not edited by hand. Rerun and republish after a game update adds items.
  - `tools/gen_econ_images.py`: `econ.css` has the tile images as `img-<name>` (equipment icons like `ak47`, skins as
    `<item>_<paint kit>`, agents, base items), `econ_wear.css` the worn renders (`_medium`, `_heavy`), `econ_stickers.css`
    the stickers as `sticker__<sticker_material with / as __>`. It also copies the game's SVG icons scaled up, since the game
    draws an SVG at its own size.
  - `tools/gen_tints.py`: `tints.css`, the graffiti tints as `img-tint_<id>` and `tint-<id>`.

Two things the game does that the styles work around:

- A flow that wraps does so early when a line is filled to the pixel, so everything that wraps leaves a pixel over.
  A scrolling panel filled to the pixel shows its scroll bar with nothing to scroll.
- Nothing inside a scrolling panel has a border: where the panel cuts a child the game cuts its fill but draws its border on,
  over whatever is past the edge.

## Building and publishing

1. Install the CS2 Workshop Tools and create an addon, e.g. `cs2menus`.
2. Run `python tools/minify.py <cs2>/content/csgo_addons/cs2menus/panorama`: a copy of `panorama/` without comments,
   indentation and blank lines, so the published addon carries nothing it doesn't need.
3. Open the addon in the tools so the layout and styles compile, then publish it to the Workshop.
4. Put the Workshop ID in `cfg/cs2menus/core.cfg` under `Panorama` > `WorkshopId`.
   With [MultiAddonManager](https://github.com/Source2ZE/MultiAddonManager) loaded, cs2menus mounts it on server start.
