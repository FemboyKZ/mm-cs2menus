"""Writes panorama/styles/custom_game/cs2menus/econ.css, an img-<name> grid class per tile image the game ships,
econ_wear.css with the worn skin renders for menu previews and econ_stickers.css with the stickers.

    pip install vpk
    py gen_econ_images.py "<CS2>/game/csgo/pak01_dir.vpk"

Rerun and republish the addon after a game update adds items, the class list is only as new as the pak it was read from.
Class names are the image file name without its "_png.vtex_c"/".vsvg_c" suffix, so a plugin can build them from items_game.txt:
- equipment icons: panorama/images/icons/equipment, like ak47 or knife_karambit
- skins: <item name>_<paint kit name>, from econ/default_generated (the "_light" render)
- worn skins, in econ_wear.css only: the same plus "_medium" or "_heavy"
- agents: the image_inventory file name, like customplayer_tm_leet_variantg, plus local_agent_t and local_agent_ct
- base items: econ/weapons/base_weapons, like weapon_knife_karambit or ct_gloves
- charms: the image_inventory file name, like kc_db_8ball
- music kits: "music__" and the image_inventory file name, like music__valve_cs2_01
- pins, coins, medals and trophies: "coin__" and the image_inventory file name, like coin__5yearcoin
- a few UI icons: the team logos t_logo and ct_logo, refresh, votescrambleteams, random and stattrak
- stickers, in econ_stickers.css only: "sticker__" and the sticker_material with "/" as "__", like sticker__dreamhack__dh_gologo1

Panorama draws an SVG at its own width and height, mostly 32px, so tiles would blur it.
Every SVG is copied into panorama/images/custom_game/cs2menus with its size raised to SVG_SIZE on the longer side.
"""

import os
import re
import shutil
import sys

import vpk

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "panorama")
OUT = os.path.join(ROOT, "styles", "custom_game", "cs2menus", "econ.css")
# Only menu.xml loads it, since it doubles the class count and only the preview uses it.
OUT_WEAR = os.path.join(ROOT, "styles", "custom_game", "cs2menus", "econ_wear.css")
SKIN_FOLDER = "panorama/images/econ/default_generated/"
WEAR_SUFFIXES = ("_medium", "_heavy")
# Only grid.xml loads it, the sticker pickers are grids.
OUT_STICKERS = os.path.join(
    ROOT, "styles", "custom_game", "cs2menus", "econ_stickers.css"
)
STICKER_FOLDER = "panorama/images/econ/stickers/"
STICKER_VARIANT = re.compile(r"(_\d+_\d+|_large)$")
CHARM_FOLDER = "panorama/images/econ/keychains/"
# (folder, class prefix): music kits, and the collectibles a scoreboard shows, pins, coins, medals and trophies.
PREFIXED_FOLDERS = [
    ("panorama/images/econ/music_kits/", "music__"),
    ("panorama/images/econ/status_icons/", "coin__"),
    ("panorama/images/econ/premier_seasons/", "coin__"),
]
SVG_FOLDER = "panorama/images/custom_game/cs2menus/"
SVG_DIR = os.path.join(ROOT, "images", "custom_game", "cs2menus")
# Sharp on a large tile at 4K.
SVG_SIZE = 512

# (folder, required suffix, suffix dropped from the class name)
SOURCES = [
    ("panorama/images/icons/equipment/", ".vsvg_c", ".vsvg_c"),
    (SKIN_FOLDER, "_light_png.vtex_c", "_light_png.vtex_c"),
    ("panorama/images/econ/characters/", "_png.vtex_c", "_png.vtex_c"),
    ("panorama/images/econ/characters/", ".vsvg_c", ".vsvg_c"),
    ("panorama/images/econ/weapons/base_weapons/", "_png.vtex_c", "_png.vtex_c"),
]

# Single files outside those folders, as class name -> compiled path.
EXTRAS = {
    # The round team logos from the team select screen.
    "t_logo": "panorama/images/icons/t_logo.vsvg_c",
    "ct_logo": "panorama/images/icons/ct_logo.vsvg_c",
    # A circular arrow, and crossing arrows in T yellow and CT blue.
    "refresh": "panorama/images/icons/ui/refresh.vsvg_c",
    "votescrambleteams": "panorama/images/icons/ui/votescrambleteams.vsvg_c",
    # A die.
    "random": "panorama/images/icons/ui/random.vsvg_c",
    # The StatTrak swap tool, its orange counter.
    "stattrak": "panorama/images/econ/tools/stattrak_swap_tool_png.vtex_c",
}

SIZE_ATTR = re.compile(r'\s(width|height)="([\d.]+)(px)?"')


def upscaled_svg(compiled):
    """The SVG source inside a vsvg_c, its root width and height scaled up to SVG_SIZE on the longer side."""
    start = compiled.find(b"<svg")
    end = compiled.rfind(b"</svg>")
    if start < 0 or end < 0:
        return None
    svg = compiled[start : end + len(b"</svg>")].decode("utf-8")
    tag_end = svg.find(">")
    tag = svg[:tag_end]
    sizes = {m.group(1): float(m.group(2)) for m in SIZE_ATTR.finditer(tag)}
    if "width" not in sizes or "height" not in sizes:
        return svg
    scale = SVG_SIZE / max(sizes["width"], sizes["height"])
    if scale <= 1:
        return svg
    # Without a viewBox the drawing would keep its size in the bigger canvas.
    if "viewBox" not in tag:
        tag += f' viewBox="0 0 {sizes["width"]:g} {sizes["height"]:g}"'
    tag = SIZE_ATTR.sub(
        lambda m: f' {m.group(1)}="{sizes[m.group(1)] * scale:g}px"', tag
    )
    return tag + svg[tag_end:]


def write_css(path, classes):
    with open(path, "w", encoding="utf-8", newline="\n") as out:
        out.write(
            "/* Generated by workshop/tools/gen_econ_images.py from the game's images, don't edit by hand. */\n"
        )
        for name in sorted(classes):
            out.write(
                f'.img-{name} {{ background-image: url("s2r://{classes[name]}"); }}\n'
            )


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    pak = vpk.open(sys.argv[1])

    paths = {}
    for path in pak:
        for folder, suffix, drop in SOURCES:
            if not path.startswith(folder) or not path.endswith(suffix):
                continue
            name = path[len(folder) : -len(drop)]
            # Subfolders and the square agent crops have no use in a tile.
            if "/" in name or name.endswith("_square"):
                continue
            paths.setdefault(name, path)
    files = set(pak)
    for name, path in EXTRAS.items():
        if path in files:
            paths.setdefault(name, path)

    shutil.rmtree(SVG_DIR, ignore_errors=True)
    os.makedirs(SVG_DIR)
    classes = {}
    for name, path in paths.items():
        svg = (
            upscaled_svg(pak.get_file(path).read())
            if path.endswith(".vsvg_c")
            else None
        )
        if svg is None:
            # s2r:// takes the compiled resource name without its trailing "_c".
            classes[name] = path[:-2]
            continue
        with open(
            os.path.join(SVG_DIR, name + ".svg"), "w", encoding="utf-8", newline="\n"
        ) as out:
            out.write(svg)
        classes[name] = SVG_FOLDER + name + ".vsvg"

    worn = {}
    stickers = {}
    for path in pak:
        if not path.endswith("_png.vtex_c"):
            continue
        if path.startswith(SKIN_FOLDER):
            name = path[len(SKIN_FOLDER) : -len("_png.vtex_c")]
            if "/" not in name and name.endswith(WEAR_SUFFIXES):
                worn[name] = path[:-2]
        elif path.startswith(STICKER_FOLDER):
            material = path[len(STICKER_FOLDER) : -len("_png.vtex_c")]
            # The 1355_37 strips and the large renders are other uses of the same sticker.
            if not STICKER_VARIANT.search(material):
                stickers["sticker__" + material.replace("/", "__")] = path[:-2]
        elif path.startswith(CHARM_FOLDER):
            classes.setdefault(os.path.basename(path)[: -len("_png.vtex_c")], path[:-2])
        else:
            for folder, prefix in PREFIXED_FOLDERS:
                name = path[len(folder) : -len("_png.vtex_c")]
                # The "_small" renders are the scoreboard's own copies.
                if (
                    path.startswith(folder)
                    and "/" not in name
                    and not name.endswith("_small")
                ):
                    classes.setdefault(prefix + name.lower(), path[:-2])

    write_css(OUT, classes)
    write_css(OUT_WEAR, worn)
    write_css(OUT_STICKERS, stickers)
    svgs = sum(1 for path in classes.values() if path.startswith(SVG_FOLDER))
    print(f"{len(classes)} classes ({svgs} upscaled SVGs) -> {os.path.normpath(OUT)}")
    print(f"{len(worn)} worn classes -> {os.path.normpath(OUT_WEAR)}")
    print(f"{len(stickers)} sticker classes -> {os.path.normpath(OUT_STICKERS)}")


if __name__ == "__main__":
    main()
