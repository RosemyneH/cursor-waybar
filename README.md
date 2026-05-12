# cursor-waybar

Small **C** helper that prints one line of **Waybar `custom` module JSON** for **personal Cursor usage**, using the same **undocumented** `https://cursor.com/api` flow as the VS Code extension **[cursor-usage-monitor](https://github.com/lixwen/cursor-usage-monitor)** (cookie `WorkosCursorSessionToken`, `GET /usage`, `GET /auth/stripe`, `POST /dashboard/get-filtered-usage-events`). Cursor may change these endpoints without notice.

Official team APIs (`https://api.cursor.com`, Basic auth with `crsr_…` keys) are documented in the [Cursor APIs Overview](https://cursor.com/docs/api); they are **not** what this binary uses.

## Build

Requires a C11 compiler and **libcurl** development headers.

```sh
make
make install   # installs cursor-waybar-usage + cursor-waybar-setup to ~/.local/bin (override PREFIX=)
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

1. Try **`sqlite3`** on `~/.config/Cursor/User/globalStorage/state.vscdb` (Cursor must have been opened and signed in at least once).
2. If that fails or you decline, prompt you to **paste** the **`WorkosCursorSessionToken`** cookie from [cursor.com](https://cursor.com) (browser devtools → Application → Cookies), or a bare JWT if you know what you are doing.

It writes **`~/.config/cursor-waybar/token`** with mode **0600**, then runs **`cursor-waybar-usage`** once to confirm the output is not an error line.

After that, Waybar can use a **plain** `exec` path to `cursor-waybar-usage` (no `CURSOR_READ_SQLITE` wrapper required).

## Token (manual / advanced)

If you skip the script, credentials are read in this order:

1. Environment: `CURSOR_SESSION_TOKEN` or `CURSOR_ACCESS_TOKEN`
2. File: `CURSOR_TOKEN_FILE`, or `$XDG_CONFIG_HOME/cursor-waybar/token`, or `~/.config/cursor-waybar/token`
3. If `CURSOR_READ_SQLITE=1`, read `cursorAuth/accessToken` from the Cursor DB via `sqlite3` on each run

Prefer the **token file** over environment variables (visible in `/proc`).

## Waybar

**HyDE dotfiles (HyDE/Configs):** for Hyde users, this repo’s layout and module are mirrored under **`HyDE/Configs/.local/share/waybar/layouts/hyprdots/05-cursor.jsonc`**, **`HyDE/Configs/.local/share/waybar/modules/custom-cursor_usage.jsonc`**, and the same **`modules/`** / **`layouts/`** / **`user-style.css`** snippets under **`HyDE/Configs/.config/waybar/`** (sync to `~/.config/waybar/`). The module `exec` uses `"$HOME/.local/bin/cursor-waybar-usage"`.

**HyDE / regenerated bar:** the string **`custom/cursor_usage`** must appear in your Waybar **`modules-*`** list or inside a **`group/...` → `modules`** array in `~/.config/waybar/config.jsonc`. A separate file under `modules/` only defines the module; Hyde’s layout generator can **drop** that name when it rewrites `config.jsonc` — re-add `"custom/cursor_usage"` after each regen if it vanishes.

**HyDE / `{0}` format:** many Hyde module configs use `"format": "{0}"`. This program sets both **`text`** and **`0`** to the same string so either `{text}` or `{0}` works.

**Offline / theme check:** `CURSOR_WAYBAR_MOCK_JSON='{"text":"Cur 0%","tooltip":"mock","percentage":0}' cursor-waybar-usage`

## Display

- **Request limits** (Pro/Business with `gpt-4` `maxRequestUsage`): `text` like `Cur 42%`, `percentage` for the bar, tooltip shows used/limit.
- **Usage-based**: aggregates **today** (local midnight → now) from filtered usage events; `text` like `Cur $0.03` (no `percentage`).
- **Plan month (dashboard):** when `api2.cursor.sh` `GetCurrentPeriodUsage` works, the **pill text** is **Auto % · API %** (one decimal when needed, similar to Cursor’s status line). **Total %**, billing cycle, Cursor `displayMessage`, and the rest (today `$` or gpt-4 counts) stay in the **tooltip** under `---`. Waybar’s `percentage` field still reflects **Total** for bar tint. If that RPC fails, the label falls back to the older `Cur $…` / request style.

## License

MIT for code in `src/` and `include/`. `third_party/cJSON.c` / `cJSON.h` are Copyright (c) Dave Gamble and contributors (MIT).
