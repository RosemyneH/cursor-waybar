# cursor-waybar

Small **C** helper that prints one line of **Waybar `custom` module JSON** for **personal Cursor usage**, using the same **undocumented** `https://cursor.com/api` flow as the VS Code extension **[cursor-usage-monitor](https://github.com/lixwen/cursor-usage-monitor)** (cookie `WorkosCursorSessionToken`, `GET /usage`, `GET /auth/stripe`, `POST /dashboard/get-filtered-usage-events`). Cursor may change these endpoints without notice.

Official team APIs (`https://api.cursor.com`, Basic auth with `crsr_…` keys) are documented in the [Cursor APIs Overview](https://cursor.com/docs/api); they are **not** what this binary uses.

## Build

Requires a C11 compiler and **libcurl** development headers.

```sh
make
make install   # installs to ~/.local/bin by default; override with PREFIX=
```

JSON parsing uses a **vendored** [cJSON](https://github.com/DaveGamble/cJSON) (`third_party/`).

## Token

Provide a Cursor session **access token** (JWT from SQLite) or full **`WorkosCursorSessionToken`** cookie value, in this order:

1. Environment: `CURSOR_SESSION_TOKEN` or `CURSOR_ACCESS_TOKEN`
2. File: `CURSOR_TOKEN_FILE` path, or `~/.config/cursor-waybar/token` (also respects `XDG_CONFIG_HOME`)
3. If `CURSOR_READ_SQLITE=1`, read `cursorAuth/accessToken` from  
   `~/.config/Cursor/User/globalStorage/state.vscdb` via the `sqlite3` CLI

Prefer a **0600** token file; environment variables are visible in `/proc`.

## Waybar

**HyDE / `{0}` format:** many Hyde module configs use `"format": "{0}"`. This program sets both **`text`** and **`0`** to the same string so either `{text}` or `{0}` works.

**Offline / theme check:** `CURSOR_WAYBAR_MOCK_JSON='{"text":"Cur 0%","tooltip":"mock","percentage":0}' cursor-waybar-usage`

## Display

- **Request limits** (Pro/Business with `gpt-4` `maxRequestUsage`): `text` like `Cur 42%`, `percentage` for the bar, tooltip shows used/limit.
- **Usage-based**: aggregates **today** (local midnight → now) from filtered usage events; `text` like `Cur $0.03` (no `percentage`).

## License

MIT for code in `src/` and `include/`. `third_party/cJSON.c` / `cJSON.h` are Copyright (c) Dave Gamble and contributors (MIT).
