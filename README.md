# cursor-waybar

Small **C** helper that prints one line of **Waybar `custom` module JSON** for **personal Cursor usage**, using the same **undocumented** `https://cursor.com/api` flow as the VS Code extension **[cursor-usage-monitor](https://github.com/lixwen/cursor-usage-monitor)** (cookie `WorkosCursorSessionToken`, `GET /usage`, `GET /auth/stripe`, `POST /dashboard/get-filtered-usage-events`). Cursor may change these endpoints without notice.

Official team APIs (`https://api.cursor.com`, Basic auth with `crsr_…` keys) are documented in the [Cursor APIs Overview](https://cursor.com/docs/api); they are **not** what this binary uses.

## Build

Requires a C11 compiler and **libcurl** development headers.

```sh
make
make install   # installs binary, scripts, and ~/.local/share/cursor-waybar/cursor-icon-fallback.png
```

JSON parsing uses a **vendored** [cJSON](https://github.com/DaveGamble/cJSON) (`third_party/`).

## First-time setup (token)

Run the helper once in a terminal:

```sh
cursor-waybar-setup
```

From a git clone (before `make install`):

```sh
bash scripts/cursor-waybar-setup
```

It will, in order:

1. If **`sqlite3`** is available and **`~/.config/Cursor/User/globalStorage/state.vscdb`** is readable (override path with **`CURSOR_STATE_DB`**), read **`cursorAuth/accessToken`** from the DB (same as when Cursor has been opened and signed in). You can confirm saving it to the token file.
2. If that fails or you decline, prompt you to **paste** the **`WorkosCursorSessionToken`** cookie from [cursor.com](https://cursor.com) (browser devtools → Application → Cookies), or a bare session JWT if you know what you are doing.

It writes **`~/.config/cursor-waybar/token`** with mode **0600**, then runs **`cursor-waybar-usage`** once to confirm the output is not an error line.

After that, Waybar can use a **plain** `exec` path to `cursor-waybar-usage` (no **`CURSOR_READ_SQLITE`** needed). Optionally set **`CURSOR_READ_SQLITE=1`** on the module if you prefer the binary to read the DB on every run instead of the file.

## Token (manual / advanced)

If you skip the script, credentials are read in this order:

1. Environment: `CURSOR_SESSION_TOKEN` or `CURSOR_ACCESS_TOKEN`
2. File: `CURSOR_TOKEN_FILE`, or `$XDG_CONFIG_HOME/cursor-waybar/token`, or `~/.config/cursor-waybar/token`
3. If `CURSOR_READ_SQLITE=1`, read `cursorAuth/accessToken` from the Cursor DB via `sqlite3` on each run (path: `CURSOR_STATE_DB` or `~/.config/Cursor/User/globalStorage/state.vscdb`)

Prefer the **token file** over environment variables (visible in `/proc`). Prefer the token file over **`CURSOR_READ_SQLITE`** (spawns `sqlite3` every Waybar refresh).

## Waybar

**HyDE dotfiles (HyDE/Configs):** for Hyde users, this repo’s layout and modules are mirrored under **`HyDE/Configs/.local/share/waybar/layouts/hyprdots/05-cursor.jsonc`**, **`HyDE/Configs/.local/share/waybar/modules/custom-cursor_usage.jsonc`**, **`HyDE/Configs/.local/share/waybar/modules/cursor-brand.jsonc`** (Waybar **`image#cursor_brand`** → real Cursor **`code.png`**), and the same **`modules/`** / **`layouts/`** / **`user-style.css`** snippets under **`HyDE/Configs/.config/waybar/`** (sync to `~/.config/waybar/`). The usage module `exec` uses `"$HOME/.local/bin/cursor-waybar-usage"`; the icon uses **`cursor-waybar-icon`**, which looks for **`resources/app/resources/linux/code.png`** next to the **`cursor`** binary (and common **`/usr/share` / `/opt`** paths). Override with **`CURSOR_ICON_PATH`** if your install is unusual.

**HyDE / regenerated bar:** **`image#cursor_brand`** and **`custom/cursor_usage`** must appear in your **`group/pill#cursor` → `modules`** array (or equivalent) in `~/.config/waybar/config.jsonc`. A separate file under `modules/` only defines the module; Hyde’s layout generator can **drop** those names when it rewrites `config.jsonc` — re-add them after each regen if they vanish.

**HyDE / `{0}` format:** many Hyde module configs use `"format": "{0}"`. This program sets both **`text`** and **`0`** to the same string so either `{text}` or `{0}` works.

**Offline / theme check:** `CURSOR_WAYBAR_MOCK_JSON='{"text":"Cur 0%","tooltip":"mock","percentage":0}' cursor-waybar-usage`

## Display

- **Request limits** (Pro/Business with `gpt-4` `maxRequestUsage`): `text` like `Cur 42%`, `percentage` for the bar, tooltip shows used/limit.
- **Usage-based**: aggregates **today** (local midnight → now) from filtered usage events; `text` like `Cur $0.03` (no `percentage`). When **`GetCurrentPeriodUsage`** also returns billing-cycle dates, the tooltip adds **Billing cycle sum** (~ USD from the same filtered-events API over **cycle start → min(now, cycle end)**), paginated up to **50** pages of 100 events (very heavy months can under-count).
- **Plan month (dashboard):** when `api2.cursor.sh` `GetCurrentPeriodUsage` works, the **pill text** is **Auto % · API %** (one decimal when needed, similar to Cursor’s status line). **Total %**, billing cycle, and the rest (today `$` or gpt-4 counts) stay in the **tooltip** under `---`. Cursor’s `displayMessage` field is **not** copied into the tooltip by default (it is often a generic promo or warning unrelated to your current numbers). Set **`CURSOR_WAYBAR_DISPLAY_MESSAGE=1`** if you want it appended. Waybar’s `percentage` field still reflects **Total** for bar tint; pill **color** uses calmer thresholds on total (high only at ≥99%). If that RPC fails, the label falls back to the older `Cur $…` / request style.

**Icon:** `cursor-waybar-icon` prefers Cursor’s **`code.png`** (static paths, **`.desktop` `Exec=`** walk-up, `cursor` on `PATH`, themed **`Icon=`**, filtered **`find`** so **VS Code**’s `/usr/share/code/.../code.png` is skipped, then a short **`find $HOME`** with **`timeout`**). Install ships a **blue fallback** if nothing matches. If it stays blue, set **`CURSOR_ICON_PATH`** to the real file (in Cursor: **Help → About** often shows the install path; look for `resources/app/resources/linux/code.png`).

## License

MIT for code in `src/` and `include/`. `third_party/cJSON.c` / `cJSON.h` are Copyright (c) Dave Gamble and contributors (MIT).
