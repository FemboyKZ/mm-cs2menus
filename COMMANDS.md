# CS2Menus Commands

cs2menus is a menu-rendering library.

- Its own commands are personal UI and navigation, plus two server-op commands.
- Most are also reachable via chat aliases.
- The server console always has full access.
- Arguments are `key=value` pairs.
  A command's first argument can go without its key, so `mm_pref_key up key=f` is `mm_pref_key action=up key=f`.

## Permissions

**Permission check order:**

1. Group overrides (`admin_groups.cfg`).
2. Global overrides (`cfg/cs2admin/admin_overrides.cfg`).
3. Default flag (below).

**If mm-cs2admin is not loaded:**

- open commands stay open.
- any command with a non-open default flag falls back to server console only.

## Commands

| Console                  | Chat alias                      | Default flag | Description                                                |
| ------------------------ | ------------------------------- | ------------ | ---------------------------------------------------------- |
| `cs2menus_reload`        | -                               | `z` root     | Reload core.cfg and re-probe HTML availability             |
| `cs2menus_version`       | -                               | `z` root     | Print plugin and interface versions                        |
| `cs2menus_panorama_diag` | -                               | `z` root     | Print panorama signatures, addon mount, clicks and windows |
| `cs2menus_demo [type]`   | -                               | `z` root     | Open a demo menu with every item type (chat/html/panorama) |
| `mm_menu_prefs`          | `!menu`, `!prefs`, `!menuprefs` | open         | Open your personal menu-preferences menu                   |
| `mm_pref_type`           | -                               | open         | Set menu style: chat/html/panorama/default                 |
| `mm_pref_key`            | -                               | open         | Set a navigation key: `<action> key=<key>`                 |
| `mm_pref_show`           | -                               | open         | Show your current preferences                              |
| `mm_pref_reset`          | -                               | open         | Reset your menu style and keys to server defaults          |
| `mm_pref_reset <target>` | -                               | `b` generic  | Reset another player's prefs (`#slot`, SteamID, name)      |
| `mm_menu_up`             | `!menu_up`                      | open         | Move the open menu's cursor up                             |
| `mm_menu_down`           | `!menu_down`                    | open         | Move the open menu's cursor down                           |
| `mm_menu_select`         | `!menu_select`, bare number     | open         | Select the highlighted / numbered item                     |
| `mm_menu_close`          | `!menu_close`                   | open         | Close the open menu                                        |

`mm_pref_reset <target>` needs strictly higher immunity than the target. Root and console bypass it.
Offline immunity can't be checked, so offline targets also need `pref_reset_offline` (default `z` root).

Navigation commands (`menu_up/down/select/close`) deny **silently** so gating them can't spam chat on every keypress.

> [!WARNING]
> Gating navigation blocks non-permitted players from using _any_ menu shown by consumer plugins,
> so leave them open unless you really intend that.

## admin_overrides.cfg examples

```json
"Overrides"
{
    "cs2menus_reload"  "i"  // let config-flag admins reload instead of root only
    "menu_prefs"       "b"  // gate the preferences menu to generic admins
    "pref_reset_other" "d"  // only ban admins can reset other players' prefs
}
```

## Flag reference

- `a` reservation
- `b` generic
- `c` kick
- `d` ban
- `e` unban
- `f` slay
- `g` changemap
- `h` convars
- `i` config
- `j` chat
- `k` vote
- `l` password
- `m` rcon
- `n` cheats
- `o`-`t` custom 1–6
- `z` root (all access)
