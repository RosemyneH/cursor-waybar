# cursor-waybar

<img width="1920" height="1080" alt="image" src="https://github.com/user-attachments/assets/e0323cdb-7245-45ad-aaec-2f74e148538f" />

Waybar helper: shows **Cursor** usage in the bar (`custom/cursor_usage`) and optional **app icon** (`image#cursor_brand`). Talks to the same unofficial flow as [cursor-usage-monitor](https://github.com/lixwen/cursor-usage-monitor); APIs can change anytime.

## Quick setup

**Needs:** C11 compiler, **libcurl** headers, **python3** (for the fallback icon PNG), **sqlite3** (optional, easiest token source).

From a clone:

```sh
bash scripts/setup
```

That **builds**, **`make install`** (into `PREFIX`, default `~/.local`), copies **Waybar snippets** into `~/.config/waybar/modules/` if they are not there yet, then runs **token** setup (`cursor-waybar-setup`).

Reload Waybar (`SIGUSR2` or your compositor’s reload).

### Other setups

| Command | What it does |
|--------|----------------|
| `bash scripts/setup --no-token` | Build, install, Waybar snippets; no token prompts (you already have `~/.config/cursor-waybar/token`). |
| `bash scripts/setup hyde` | Prints **HyDE** layout hints, then same as default (install + token). Does **not** copy into `~/.config/waybar/modules/` — you place the JSONC in your HyDE tree. |
| `bash scripts/setup hyde --no-token` | HyDE hints + install; no token step. |

**Custom Waybar config dir:** `WAYBAR_CONFIG_HOME=/path/to/waybar bash scripts/setup-waybar-snippets` (after `make install`).

**Only Waybar files:** `bash scripts/setup-waybar-snippets`  
**Only token:** `cursor-waybar-setup` (or `bash scripts/cursor-waybar-setup` before install).

## Wire Waybar

1. Ensure your main config **includes** the `modules/` dir (many setups already do).
2. Add **`custom/cursor_usage`** and **`image#cursor_brand`** to a **group** in `config.jsonc` (exact layout is yours).

Snippets use `"$HOME/.local/bin/cursor-waybar-usage"` and `cursor-waybar-icon`. Override paths in the JSONC if `PREFIX` is not `~/.local`.

**Icon wrong or missing:** set `CURSOR_ICON_PATH` to Cursor’s `code.png`, or see `scripts/cursor-waybar-icon`.

## Token (short)

Setup writes **`~/.config/cursor-waybar/token`** (0600). Without it, the binary can use **`CURSOR_READ_SQLITE=1`** to read Cursor’s DB each run (heavier). See `scripts/cursor-waybar-setup` for DB vs browser cookie flow.

Usage **tooltips** (usage-based billing) use **`/api/dashboard/get-aggregated-usage-events`** (same totals as the billing page) for today and the current cycle, with **`get-filtered-usage-events`** only as a paginated fallback.

## License

MIT for `src/` and `include/`. `third_party/cJSON.*` — Dave Gamble et al. (MIT).
