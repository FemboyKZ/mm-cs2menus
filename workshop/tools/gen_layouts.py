"""Builds grid.xml, showcase.xml, studio.xml, columns.xml and table.xml, the layouts whose slots repeat.
menu.xml and notice.xml are written by hand.
The slot counts are the constants in src/render/panorama_hud.h named beside each below, keep the two in step.

    py gen_layouts.py
"""

import os
import re
import xml.etree.ElementTree as ET

LAYOUTS = os.path.join(
    os.path.dirname(os.path.abspath(__file__)),
    "..",
    "panorama",
    "layout",
    "custom_game",
    "cs2menus",
)

TABS = 10  # kGridTabs, kShowcaseTabs
CHIPS = 6  # kChipSlots
LIST = 16  # kListSlots
STEPS = 4  # kStepButtons
TILES = 24  # kGridSlots
BUTTONS = 24  # kShowcaseSlots
INFO_ROWS = 10  # kInfoRows
INFO_BANDS = 5  # kInfoBands
CONTROLS = 18  # kStudioControls
CONTROL_TABS = 4  # kStudioControlTabs
HINT_PARTS = 5  # kHintParts
HINT_KEYS = 4  # kHintKeys
HELP_ROWS = 8  # kHelpRows
HELP_KEYS = 3  # kHelpKeys
COLUMNS = 4  # kColumns
COLUMN_ROWS = 16  # kColumnRows
TABLE_ROWS = 18  # kTableRows
TABLE_KEYS = 30  # kTableKeys
TABLE_CELLS = 12  # kTableCells
TABLE_COLUMNS = 6  # kTableColumns


def ind(lines, levels=1):
    return ["  " * levels + line for line in lines]


def label(p, name, cls, extra=""):
    return f'<Label id="{p}{name}" class="{cls}"{extra} text="{{s:{p}{name}}}" />'


def styles(*names):
    return (
        ["<styles>"]
        + [
            f'  <include src="file://{{resources}}/styles/custom_game/cs2menus/{name}.css" />'
            for name in names
        ]
        + ["</styles>"]
    )


def header(p):
    return [
        '<Panel class="window-header">',
        "  " + label(p, "title", "menu-title"),
        f'  <Button id="{p}scope" class="scope-chip hidden">',
        '    <Panel class="scope-dot scope-dot-t" />',
        '    <Panel class="scope-dot scope-dot-ct" />',
        "    " + label(p, "scope_lbl", "scope-label"),
        "  </Button>",
        "  " + label(p, "edited", "edited-mark"),
        f'  <Button id="{p}back" class="hist-btn back-btn disabled" />',
        f'  <Button id="{p}forward" class="hist-btn forward-btn disabled" />',
        f'  <Button id="{p}refresh" class="hist-btn refresh-btn disabled" />',
        f'  <Button id="{p}collapse" class="hist-btn collapse-btn" />',
        f'  <Button id="{p}close" class="close-btn" />',
        "</Panel>",
    ]


# The tab row: the tabs, and `end` at their end, the page arrows or in the columns the chips.
# A tab's dot shows on a marked one, its caret on the one the rest hide behind.
def tabrow(p, end):
    return ['<Panel class="tabrow">'] + ind(tabs(p) + end) + ["</Panel>"]


def tabs(p):
    out = [f'<Panel id="{p}pages" class="tabs">']
    for i in range(TABS):
        out += [
            f'  <Button id="{p}nav{i}" class="tab hidden">',
            '    <Panel class="tab-dot" />',
            "    " + label(p, f"nav_lbl{i}", "tab-label"),
            '    <Panel class="tab-caret" />',
            "  </Button>",
        ]
    return out + ["</Panel>"]


def input_bar(p):
    return [
        f'<Button id="{p}input" class="input-bar hidden">',
        '  <Panel class="input-icon" />',
        "  " + label(p, "input_text", "input-text"),
        "  " + label(p, "input_hint", "input-hint"),
        f'  <Button id="{p}input_x" class="input-x" />',
        "</Button>",
    ]


def chips(p):
    out = [f'<Panel id="{p}chips" class="chips hidden">']
    for i in range(CHIPS):
        out += [
            f'  <Button id="{p}chip{i}" class="chip hidden">',
            "    " + label(p, f"chip_lbl{i}", "chip-label"),
            "    " + label(p, f"chip_val{i}", "chip-value"),
            '    <Panel class="chip-caret" />',
            "  </Button>",
        ]
    return out + ["</Panel>"]


def empty(p):
    return [
        f'<Panel id="{p}empty" class="empty-state hidden">',
        "  " + label(p, "empty_title", "empty-title"),
        "  " + label(p, "empty_text", "empty-text"),
        "</Panel>",
    ]


def pager(p):
    return [
        f'<Panel id="{p}pager" class="pager hidden">',
        f'  <Button id="{p}prev" class="popup-btn"><Label class="popup-btn-label" text="&lt;" /></Button>',
        "  " + label(p, "page", "popup-page"),
        f'  <Button id="{p}next" class="popup-btn"><Label class="popup-btn-label" text="&gt;" /></Button>',
        "</Panel>",
    ]


def step_popup(p):
    def button(i):
        return f'  <Button id="{p}step_b{i}" class="step-btn">{label(p, f"step_l{i}", "step-btn-label")}</Button>'

    half = STEPS // 2
    return (
        [
            f'<Panel id="{p}step" class="popup step-popup hidden">',
            '  <Panel class="window-header">',
            "    " + label(p, "step_title", "popup-title"),
            f'    <Button id="{p}step_close" class="close-btn" />',
            "  </Panel>",
            '  <Panel class="step-row">',
        ]
        + ind([button(i) for i in range(half)])
        + ["    " + label(p, "step_val", "step-readout")]
        + ind([button(i) for i in range(half, STEPS)])
        + ["  </Panel>", "</Panel>"]
    )


# A row's dot shows on a marked one, its second line when it has one.
def list_popup(p):
    out = [
        f'<Panel id="{p}list" class="popup list-popup hidden">',
        '  <Panel class="window-header">',
        "    " + label(p, "list_title", "popup-title"),
        f'    <Button id="{p}list_close" class="close-btn" />',
        "  </Panel>",
        '  <Panel class="list-rows">',
    ]
    for i in range(LIST):
        out += [
            f'    <Button id="{p}li{i}" class="li hidden">',
            '      <Panel class="li-dot" />',
            '      <Panel class="li-text">',
            "        " + label(p, f"li_lbl{i}", "li-label"),
            "        " + label(p, f"li_sub{i}", "li-sub"),
            "      </Panel>",
            "    </Button>",
        ]
    return out + [
        "  </Panel>",
        '  <Panel class="popup-nav">',
        f'    <Button id="{p}list_prev" class="popup-btn"><Label class="popup-btn-label" text="&lt;" /></Button>',
        "    " + label(p, "list_page", "popup-page"),
        f'    <Button id="{p}list_next" class="popup-btn"><Label class="popup-btn-label" text="&gt;" /></Button>',
        "  </Panel>",
        "</Panel>",
    ]


def preview(p):
    return [
        f'<Panel id="{p}preview" class="popup preview-popup hidden">',
        f'  <Panel id="{p}preview_img" class="preview-img" />',
        "</Panel>",
    ]


def message(p):
    return [
        f'<Panel id="{p}msg" class="msg hidden">',
        '  <Panel class="msg-dot" />',
        "  " + label(p, "msg_text", "msg-text"),
        "</Panel>",
    ]


def dialog(p):
    return [
        f'<Button id="{p}dlg" class="dlg-scrim hidden">',
        f'  <Button id="{p}dlg_box" class="dlg-box">',
        "    " + label(p, "dlg_title", "dlg-title"),
        "    " + label(p, "dlg_body", "dlg-body"),
        '    <Panel class="dlg-btns">',
        f'      <Button id="{p}dlg_no" class="dlg-btn dlg-no">{label(p, "dlg_no_lbl", "dlg-btn-label")}</Button>',
        f'      <Button id="{p}dlg_yes" class="dlg-btn dlg-yes">{label(p, "dlg_yes_lbl", "dlg-btn-label")}</Button>',
        "    </Panel>",
        "  </Button>",
        "</Button>",
    ]


def info(p):
    out = [
        f'<Panel id="{p}info" class="info-card hidden">',
        "  " + label(p, "info_title", "info-title"),
        "  " + label(p, "info_sub", "info-sub"),
        f'  <Panel id="{p}info_meter" class="info-meter hidden">',
        '    <Panel class="info-bar">',
    ]
    out += [
        f'      <Panel id="{p}info_band{i}" class="info-band info-band{i}" />'
        for i in range(INFO_BANDS)
    ]
    out += [
        "    </Panel>",
        '    <Panel class="info-dims">',
        f'      <Panel id="{p}info_dim_l" class="info-dim" />',
        f'      <Panel id="{p}info_gap" class="info-gap" />',
        f'      <Panel id="{p}info_dim_r" class="info-dim" />',
        "    </Panel>",
        '    <Panel class="info-markrow">',
        f'      <Panel id="{p}info_mark_sp" class="info-mark-sp" />',
        '      <Panel class="info-mark" />',
        "    </Panel>",
        '    <Panel class="info-read">',
        "      " + label(p, "info_mlabel", "info-mlabel"),
        "      " + label(p, "info_mvalue", "info-mvalue"),
        "    </Panel>",
        "  </Panel>",
        '  <Panel class="info-rows">',
    ]
    for i in range(INFO_ROWS):
        out += [
            f'    <Panel id="{p}info_row{i}" class="info-row hidden">',
            "      " + label(p, f"info_rl{i}", "info-rl"),
            "      " + label(p, f"info_rv{i}", "info-rv"),
            "    </Panel>",
        ]
    return out + ["  </Panel>", "</Panel>"]


# The studio's key list, in the info card's place while it's open: a row of key caps and what they do.
def help_card(p):
    out = [
        f'<Panel id="{p}helpcard" class="help-card hidden">',
        "  " + label(p, "help_title", "info-title"),
    ]
    for i in range(HELP_ROWS):
        out.append(f'  <Panel id="{p}hrow{i}" class="help-row hidden">')
        out.append('    <Panel class="help-keys">')
        out += [
            "      " + label(p, f"hk{i}_{k}", "hkey hidden") for k in range(HELP_KEYS)
        ]
        out.append("    </Panel>")
        out.append("    " + label(p, f"ht{i}", "help-text"))
        out.append("  </Panel>")
    return out + ["</Panel>"]


def badges(p, n):
    return [
        '<Panel class="tile-badges">',
        "  " + label(p, f"tag{n}", "tile-tag"),
        '  <Panel class="tile-lock" />',
        '  <Panel class="tile-dot tile-dot-t" />',
        '  <Panel class="tile-dot tile-dot-ct" />',
        "</Panel>",
    ]


# A grid tile. Without an image its name is large and the aux line under it carries its tag.
def grid_tile(p, n):
    return (
        [f'<Button id="{p}item{n}" class="tile hidden">']
        + ind(badges(p, n))
        + [
            f'  <Button id="{p}corner{n}" class="tile-corner" />',
            f'  <Panel id="{p}img{n}" class="tile-img" />',
            "  " + label(p, f"name{n}", "tile-name"),
            "  " + label(p, f"aux{n}", "tile-aux"),
            "  " + label(p, f"val{n}", "tile-sub"),
            "  " + label(p, f"full{n}", "tile-full"),
            '  <Panel class="tile-rarity" />',
            "</Button>",
        ]
    )


# A showcase or studio button: its plain labels, and an image tile shown in their place for an item with an image.
# The tile has labels of its own, restyling the plain ones between the two left them blank until the HUD was laid out again.
def button(p, n):
    return (
        [f'<Button id="{p}item{n}" class="sbtn hidden">', '  <Panel class="sbtn-tile">']
        + ind(badges(p, n), 2)
        + [
            f'    <Button id="{p}corner{n}" class="tile-corner" />',
            f'    <Panel id="{p}img{n}" class="sbtn-img" />',
            "    " + label(p, f"iname{n}", "sbtn-iname"),
            "    " + label(p, f"ival{n}", "sbtn-isub"),
            "    " + label(p, f"full{n}", "tile-full"),
            '    <Panel class="tile-rarity" />',
            "  </Panel>",
            "  " + label(p, f"name{n}", "sbtn-name"),
            "  " + label(p, f"btag{n}", "sbtn-tag"),
            "  " + label(p, f"val{n}", "sbtn-sub"),
            "</Button>",
        ]
    )


def buttons(p):
    out = ['<Panel class="sbtns">'] + ind(empty(p))
    for n in range(BUTTONS):
        out += ind(button(p, n))
    return out + ["</Panel>"]


# The pinned item's button, the secondary item's small one before it.
def action(p):
    return [
        '<Panel class="action-row">',
        f'  <Button id="{p}action2" class="action2-btn hidden">',
        "    " + label(p, "action2_lbl", "action2-label"),
        "  </Button>",
        f'  <Button id="{p}action" class="action-btn hidden">',
        "    " + label(p, "action_lbl", "action-label"),
        "  </Button>",
        "</Panel>",
    ]


def document(style_names, root):
    return (
        ["<root>"]
        + ind(styles(*style_names))
        + ['  <Panel class="root" hittest="false">']
        + ind(root, 2)
        + ["  </Panel>", "</root>"]
    )


def grid():
    p = "cg_"
    tiles = [f'<Panel id="{p}tiles" class="tiles">'] + ind(empty(p))
    for n in range(TILES):
        tiles += ind(grid_tile(p, n))
    tiles.append("</Panel>")
    box = (
        ['<Panel class="menu-box">']
        + ind(header(p) + tabrow(p, pager(p)) + input_bar(p) + chips(p) + tiles)
        + ["</Panel>"]
    )
    root = (
        [f'<Panel id="{p}root" class="menu hidden">']
        + ind(box + step_popup(p) + list_popup(p) + preview(p) + message(p) + dialog(p))
        + ["</Panel>"]
    )
    return document(
        ("fonts", "menu", "grid", "badges", "tints", "econ", "econ_stickers"), root
    )


# One item's editor: its image on the left with the info card under it, the buttons on the right with the pinned item's
# button under them.
def showcase():
    p = "cx_"
    stage = (
        ['<Panel class="stage">', f'  <Panel id="{p}shot" class="shot" />']
        + ind(info(p))
        + ["</Panel>"]
    )
    controls = ['<Panel class="controls">'] + ind(buttons(p) + action(p)) + ["</Panel>"]
    body = (
        ['<Panel class="body">']
        + ind(
            tabrow(p, pager(p))
            + input_bar(p)
            + chips(p)
            + ['<Panel class="showcase">']
            + ind(stage + controls)
            + ["</Panel>"]
        )
        + ["</Panel>"]
    )
    box = ['<Panel class="menu-box">'] + ind(header(p) + body) + ["</Panel>"]
    root = (
        [f'<Panel id="{p}root" class="menu hidden">']
        + ind(box + step_popup(p) + list_popup(p) + message(p) + dialog(p))
        + ["</Panel>"]
    )
    return document(
        (
            "fonts",
            "menu",
            "grid",
            "info",
            "showcase",
            "buttons",
            "badges",
            "econ",
            "econ_wear",
            "econ_stickers",
        ),
        root,
    )


# The showcase's buttons in a box at the right edge, the pinned item's button under them. cs_stage fills the screen behind
# everything, the control panel and the info card sit at the left edge. Nothing in the hint pill takes clicks, the stage
# is under it. The help button at the control tabs' end swaps the info card for the key list.
def studio():
    p = "cs_"
    hint = ['<Panel class="hint-pill" hittest="false">']
    for i in range(HINT_PARTS):
        hint.append(
            f'  <Panel id="{p}hint{i}" class="hpart hidden{" first" if i == 0 else ""}" hittest="false">'
        )
        hint.append('    <Panel class="hsep" hittest="false" />')
        hint += [
            "    " + label(p, f"hkey{i}_{k}", "hkey hidden", ' hittest="false"')
            for k in range(HINT_KEYS)
        ]
        hint.append("    " + label(p, f"htxt{i}", "htxt", ' hittest="false"'))
        hint.append("  </Panel>")
    hint.append("</Panel>")

    panel = [
        f'<Panel id="{p}controls" class="controls-panel hidden">',
        f'  <Panel id="{p}ctabs" class="ctabs hidden">',
    ]
    for i in range(CONTROL_TABS):
        panel += [
            f'    <Button id="{p}ctab{i}" class="ctab hidden">',
            "      " + label(p, f"ctab_lbl{i}", "ctab-label"),
            "    </Button>",
        ]
    panel += [
        f'    <Button id="{p}help" class="help-btn hidden">',
        '      <Label class="help-btn-label" text="?" />',
        "    </Button>",
    ]
    panel.append("  </Panel>")
    for i in range(CONTROLS):
        panel += [
            f'  <Button id="{p}ctl{i}" class="cbtn hidden">',
            "    " + label(p, f"ctl_lbl{i}", "cbtn-label"),
            "    " + label(p, f"ctl_sub{i}", "cbtn-sub"),
            "  </Button>",
        ]
    panel.append("</Panel>")
    left = (
        ['<Panel class="left-col">']
        + ind(panel + info(p) + help_card(p))
        + ["</Panel>"]
    )

    controls = ['<Panel class="controls">'] + ind(buttons(p) + action(p)) + ["</Panel>"]
    body = (
        ['<Panel class="body">']
        + ind(tabrow(p, pager(p)) + input_bar(p) + chips(p) + controls)
        + ["</Panel>"]
    )
    box = ['<Panel class="menu-box studio-box">'] + ind(header(p) + body) + ["</Panel>"]
    root = (
        [
            f'<Panel id="{p}root" class="menu hidden">',
            f'  <Button id="{p}stage" class="stage-area" />',
        ]
        + ind(
            hint + left + box + step_popup(p) + list_popup(p) + message(p) + dialog(p)
        )
        + ["</Panel>"]
    )
    return document(
        (
            "fonts",
            "menu",
            "grid",
            "info",
            "studio",
            "buttons",
            "badges",
            "econ",
            "econ_stickers",
        ),
        root,
    )


# Columns of tiles that scroll on their own, for everything at a glance. Tile N is column N / COLUMN_ROWS.
# The chips sit at the end of the tab row.
def columns():
    p = "cl_"

    def tile(n):
        return (
            [
                f'<Button id="{p}item{n}" class="col-tile hidden">',
                '  <Panel class="col-tile-pic">',
            ]
            + ind(badges(p, n), 2)
            + [
                f'    <Panel id="{p}img{n}" class="col-tile-img" />',
                '    <Panel class="tile-rarity" />',
                "  </Panel>",
                '  <Panel class="col-tile-text">',
                "    " + label(p, f"name{n}", "col-tile-name"),
                "    " + label(p, f"val{n}", "col-tile-sub"),
                "  </Panel>",
                f'  <Button id="{p}corner{n}" class="tile-corner" />',
                "</Button>",
            ]
        )

    cols = [f'<Panel id="{p}cols" class="cols">'] + ind(empty(p))
    for c in range(COLUMNS):
        cols += [
            f'  <Panel id="{p}col{c}" class="col hidden">',
            '    <Panel class="col-head">',
            "      " + label(p, f"colh{c}", "col-head-name"),
            "      " + label(p, f"colc{c}", "col-head-count"),
            "    </Panel>",
            '    <Panel class="col-scroll">',
        ]
        for r in range(COLUMN_ROWS):
            cols += ind(tile(c * COLUMN_ROWS + r), 3)
        cols += ["    </Panel>", "  </Panel>"]
    cols.append("</Panel>")

    box = (
        ['<Panel class="menu-box">']
        + ind(header(p) + tabrow(p, chips(p)) + input_bar(p) + cols)
        + ["</Panel>"]
    )
    root = (
        [f'<Panel id="{p}root" class="menu hidden">']
        + ind(box + list_popup(p) + message(p) + dialog(p))
        + ["</Panel>"]
    )
    return document(
        (
            "fonts",
            "menu",
            "grid",
            "badges",
            "columns",
            "tints",
            "econ",
            "econ_stickers",
        ),
        root,
    )


# A row per item: its name, then its cells under the headings and the button for its details.
# The keys over them open a page, the chips and the page arrows sit at that row's end.
def table():
    p = "ct_"
    keys = [f'<Panel id="{p}pages" class="keys">']
    for i in range(TABLE_KEYS):
        keys += [
            f'  <Button id="{p}nav{i}" class="key hidden">',
            "    " + label(p, f"nav_lbl{i}", "key-label"),
            "  </Button>",
        ]
    keys.append("</Panel>")
    row = ['<Panel class="tabrow">'] + ind(keys + chips(p) + pager(p)) + ["</Panel>"]

    heads = ['<Panel class="thead">']
    for i in range(TABLE_COLUMNS):
        heads += [
            f'  <Button id="{p}head{i}" class="thead-col hidden">',
            "    " + label(p, f"head_lbl{i}", "thead-label"),
            '    <Panel class="thead-arrow" />',
            "  </Button>",
        ]
    heads.append("</Panel>")

    rows = ['<Panel class="trows">'] + ind(empty(p))
    for n in range(TABLE_ROWS):
        rows.append(f'  <Button id="{p}item{n}" class="trow hidden">')
        rows.append("    " + label(p, f"name{n}", "trow-name"))
        rows += [
            "    " + label(p, f"c{n}_{k}", "tcell hidden") for k in range(TABLE_CELLS)
        ]
        rows += [
            f'    <Button id="{p}corner{n}" class="trow-more">',
            '      <Label class="trow-more-label" text="..." />',
            "    </Button>",
            "  </Button>",
        ]
    rows.append("</Panel>")

    box = (
        ['<Panel class="menu-box">']
        + ind(header(p) + row + input_bar(p) + heads + rows)
        + ["</Panel>"]
    )
    root = (
        [f'<Panel id="{p}root" class="menu hidden">']
        + ind(box + list_popup(p) + message(p) + dialog(p))
        + ["</Panel>"]
    )
    return document(("fonts", "menu", "grid", "table"), root)


def write(name, lines):
    text = "\n".join(lines) + "\n"
    ET.fromstring(text)
    ids = re.findall(r'id="([^"]+)"', text)
    assert len(ids) == len(set(ids)), f"{name}: duplicate ids"
    variables = set(re.findall(r"\{s:([^}]+)\}", text))
    assert variables <= set(ids), f"{name}: a dialog variable without its label"
    open(os.path.join(LAYOUTS, name), "w", encoding="utf-8", newline="\n").write(text)
    print(f"{name}: {len(ids)} ids, {len(variables)} dialog variables")


def main():
    write("grid.xml", grid())
    write("showcase.xml", showcase())
    write("studio.xml", studio())
    write("columns.xml", columns())
    write("table.xml", table())


if __name__ == "__main__":
    main()
