# cursor-waybar

<img width="1920" height="1080" alt="image" src="https://github.com/user-attachments/assets/d4ca3356-38cd-4d66-b9fc-1a6716d0153b" />

Waybar helper: shows **Cursor** usage in the bar (`custom/cursor_usage`) and optional **app icon** (`image#cursor_brand`). Talks to the same unofficial flow as [cursor-usage-monitor](https://github.com/lixwen/cursor-usage-monitor); APIs can change anytime.

## Quick setup

**Needs:** C11 compiler, **libcurl** headers, **python3** (for the fallback icon PNG), **sqlite3** (optional, easiest token source).

From a clone:

```sh
bash scripts/setup
```

That **builds**, **`make install`** (into `PREFIX`, default `~/.local`), copies **Waybar snippets** into `~/.config/waybar/modules/` if they are not there yet, refreshes the **HyDE** layout **`hyprdots/05-cursor`**, installs **`includes/cursor-waybar.css`** and prepends its **`@import`** to **`user-style.css`** when missing, then runs **token** setup (`cursor-waybar-setup`).

Reload Waybar (`SIGUSR2` or your compositor’s reload).

### Other setups

| Command | What it does |
|--------|----------------|
| `bash scripts/setup --no-token` | Build, install, Waybar snippets + HyDE `05-cursor` layout; no token prompts (you already have `~/.config/cursor-waybar/token`). |
| `bash scripts/setup hyde` | Prints a short HyDE layout-picker hint; otherwise same as default (modules, `05-cursor` layout, install + token). |
| `bash scripts/setup hyde --no-token` | Hint + install; no token step. |

**Custom Waybar config dir:** `WAYBAR_CONFIG_HOME=/path/to/waybar bash scripts/setup-waybar-snippets` (after `make install`). **HyDE layout parent dir:** `WAYBAR_LAYOUTS_HOME` (default `$XDG_DATA_HOME/waybar/layouts`; layout is `hyprdots/05-cursor.jsonc`).

**Only Waybar files:** `bash scripts/setup-waybar-snippets`  
**Only token:** `cursor-waybar-setup` (or `bash scripts/cursor-waybar-setup` before install).

## Wire Waybar

1. Ensure your main config **includes** the `modules/` dir (many setups already do).
2. **HyDE:** run `./scripts/setup` (or `setup-waybar-snippets`), then choose Waybar layout **`hyprdots/05-cursor`** in your bar / wbar config UI. That layout uses **`group/pill#left0`** (icon + usage) **before** workspaces; **`includes/cursor-waybar.css`** adds hover styling using HyDE’s `@wb-hvr-bg` / `@wb-hvr-fg`.
3. **Other setups:** add the modules to a **group** in `config.jsonc` and **`@import "includes/cursor-waybar.css"`** from `user-style.css` (or copy rules from that file).

Snippets use `"$HOME/.local/bin/cursor-waybar-usage"` and `cursor-waybar-icon`. Override paths in the JSONC if `PREFIX` is not `~/.local`.

**Multiline tooltips:** [Waybar’s custom module wiki](https://github.com/Alexays/Waybar/wiki/Module:-Custom) expects **carriage return (`\r`)** between tooltip lines, not newline. `cursor-waybar-usage` emits `\r` and normalizes API text so the full billing / usage tooltip shows on hover.

**Waybar 0.15.x:** Do **not** set `tooltip-format` to `"{tooltip}"` — that release formats tooltips with `{fmt}` but does **not** pass a `tooltip` format arg, so the placeholder is never filled and GTK gets bad markup (empty tooltip). Leave `tooltip-format` unset so Waybar uses the JSON `tooltip` string directly. Use **`"format": "{text}"`** (not `{0}`) for the same reason.

**Icon wrong or missing:** set `CURSOR_ICON_PATH` to Cursor’s `code.png`, or see `scripts/cursor-waybar-icon`.

## Token (short)

Setup writes **`~/.config/cursor-waybar/token`** (0600). Without it, the binary can use **`CURSOR_READ_SQLITE=1`** to read Cursor’s DB each run (heavier). See `scripts/cursor-waybar-setup` for DB vs browser cookie flow.

Usage **tooltips** (usage-based billing) use **`/api/dashboard/get-aggregated-usage-events`** (same totals as the billing page) for today and the current cycle, with **`get-filtered-usage-events`** only as a paginated fallback.

## License

MIT for `src/` and `include/`. `third_party/cJSON.*` — Dave Gamble et al. (MIT).
