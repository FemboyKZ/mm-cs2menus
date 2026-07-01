# CS2Menus Commands

cs2menus is a menu-rendering library.

- Its own commands are personal UI and navigation, plus two server-op commands.
- Most are also reachable via chat aliases.
- The server console always has full access.

## Permissions

1. Group overrides (`admin_groups.cfg`).
2. Global overrides (`cfg/cs2admin/admin_overrides.cfg`).
3. Default flag (below).

**If mm-cs2admin is not loaded:**

- open commands stay open.
- `cs2menus_reload` and `cs2menus_version` fall back to server console only.

## Commands

| Console            | Chat alias                      | Default flag | Description                                    |
| ------------------ | ------------------------------- | ------------ | ---------------------------------------------- |
| `cs2menus_reload`  | -                               | `z` root     | Reload core.cfg and re-probe HTML availability |
| `cs2menus_version` | -                               | `z` root     | Print plugin and interface versions            |
| `mm_menu_prefs`    | `!menu`, `!prefs`, `!menuprefs` | open         | Open your personal menu-preferences menu       |
| `mm_pref_type`     | -                               | open         | Set menu style: chat / html / default          |
| `mm_pref_key`      | -                               | open         | Set a navigation key                           |
| `mm_pref_show`     | -                               | open         | Show your current preferences                  |
| `mm_menu_up`       | `!menu_up`                      | open         | Move the open menu's cursor up                 |
| `mm_menu_down`     | `!menu_down`                    | open         | Move the open menu's cursor down               |
| `mm_menu_select`   | `!menu_select`, bare number     | open         | Select the highlighted / numbered item         |
| `mm_menu_close`    | `!menu_close`                   | open         | Close the open menu                            |

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
