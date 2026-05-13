#define _GNU_SOURCE
#include "cursor_waybar.h"

#include <curl/curl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static long long today_start_ms_local(void)
{
	time_t t = time(NULL);
	struct tm tm_local;
	localtime_r(&t, &tm_local);
	tm_local.tm_hour = 0;
	tm_local.tm_min = 0;
	tm_local.tm_sec = 0;
	time_t midnight = mktime(&tm_local);
	return (long long)midnight * 1000LL;
}

static char *read_file_trim(cw_arena *arena, const char *path)
{
	FILE *f = fopen(path, "rb");
	if (!f)
		return NULL;
	if (fseek(f, 0, SEEK_END) != 0) {
		fclose(f);
		return NULL;
	}
	long n = ftell(f);
	if (n < 0) {
		fclose(f);
		return NULL;
	}
	rewind(f);
	char *buf = cw_arena_alloc(arena, (size_t)n + 1);
	if (!buf) {
		fclose(f);
		return NULL;
	}
	size_t r = fread(buf, 1, (size_t)n, f);
	fclose(f);
	buf[r] = '\0';
	while (r > 0 && (buf[r - 1] == '\n' || buf[r - 1] == '\r')) {
		buf[r - 1] = '\0';
		r--;
	}
	return buf;
}

static int read_sqlite_access_token(char *out, size_t outsz)
{
	const char *home = getenv("HOME");
	if (!home || !outsz)
		return -1;
	const char *db = getenv("CURSOR_STATE_DB");
	char pathbuf[1024];
	const char *vscdb;
	if (db && db[0])
		vscdb = db;
	else {
		if (snprintf(pathbuf, sizeof pathbuf,
			     "%s/.config/Cursor/User/globalStorage/state.vscdb",
			     home) >= (int)sizeof pathbuf)
			return -1;
		vscdb = pathbuf;
	}
	char cmd[1400];
	if (snprintf(cmd, sizeof cmd,
		     "sqlite3 \"%s\" "
		     "\"SELECT value FROM ItemTable WHERE key = "
		     "'cursorAuth/accessToken' LIMIT 1;\" 2>/dev/null",
		     vscdb) >= (int)sizeof cmd)
		return -1;
	FILE *p = popen(cmd, "r");
	if (!p)
		return -1;
	if (!fgets(out, (int)outsz, p)) {
		pclose(p);
		return -1;
	}
	pclose(p);
	size_t L = strlen(out);
	while (L > 0 && (out[L - 1] == '\n' || out[L - 1] == '\r')) {
		out[L - 1] = '\0';
		L--;
	}
	return L > 0 ? 0 : -1;
}

static const char *default_token_path(char *buf, size_t buflen)
{
	const char *xdg = getenv("XDG_CONFIG_HOME");
	if (xdg && xdg[0])
		snprintf(buf, buflen, "%s/cursor-waybar/token", xdg);
	else {
		const char *h = getenv("HOME");
		if (!h)
			return NULL;
		snprintf(buf, buflen, "%s/.config/cursor-waybar/token", h);
	}
	return buf;
}

static void emit_waybar_error(const char *msg)
{
	cJSON *o = cJSON_CreateObject();
	cJSON_AddStringToObject(o, "text", "?");
	cJSON_AddStringToObject(o, "0", "?");
	cJSON_AddStringToObject(o, "tooltip", msg);
	cJSON_AddStringToObject(o, "class", "cursor-usage-err");
	char *s = cJSON_PrintUnformatted(o);
	cJSON_Delete(o);
	if (s) {
		puts(s);
		free(s);
	}
}

static const char *usage_class(int pct)
{
	if (pct >= 95)
		return "cursor-usage-high";
	if (pct >= 80)
		return "cursor-usage-mid";
	return "cursor-usage-low";
}

static const char *plan_usage_class(int total_pct)
{
	if (total_pct < 0)
		return "cursor-usage-low";
	if (total_pct >= 99)
		return "cursor-usage-high";
	if (total_pct >= 88)
		return "cursor-usage-mid";
	return "cursor-usage-low";
}

static void fmt_pct_label(char *dst, size_t cap, double x)
{
	if (!isfinite(x) || cap < 4) {
		dst[0] = '\0';
		return;
	}
	if (fabs(x - round(x)) < 0.05)
		snprintf(dst, cap, "%d%%", (int)round(x));
	else
		snprintf(dst, cap, "%.1f%%", x);
}

static void build_auto_api_pill(char *dst, size_t cap, double auto_raw,
				double api_raw, int total_pct_fallback)
{
	char a[20], b[20];
	fmt_pct_label(a, sizeof a, auto_raw);
	fmt_pct_label(b, sizeof b, api_raw);
	if (a[0] && b[0])
		snprintf(dst, cap, "%s · %s", a, b);
	else if (a[0])
		snprintf(dst, cap, "%s", a);
	else if (b[0])
		snprintf(dst, cap, "%s", b);
	else if (total_pct_fallback >= 0)
		snprintf(dst, cap, "Cur %d%%", total_pct_fallback);
	else
		snprintf(dst, cap, "—");
}

static int waybar_tooltip_markup(void)
{
	const char *e = getenv("CURSOR_WAYBAR_TOOLTIP_MARKUP");
	return e && e[0] == '1';
}

static const char *tooltip_token_accent(void)
{
	const char *c = getenv("CURSOR_WAYBAR_TOOLTIP_ACCENT");
	return (c && c[0]) ? c : "#7dcfff";
}

static void fmt_tokens_pretty(char *dst, size_t cap, long long n)
{
	char buf[32];
	size_t L;

	if (!dst || cap < 4) {
		if (dst && cap)
			dst[0] = '\0';
		return;
	}
	if (n <= 0) {
		snprintf(dst, cap, "0");
		return;
	}
	if (n >= 1000000000LL)
		snprintf(buf, sizeof buf, "%.1fB", (double)n / 1e9);
	else if (n >= 1000000LL)
		snprintf(buf, sizeof buf, "%.1fM", (double)n / 1e6);
	else if (n >= 1000LL)
		snprintf(buf, sizeof buf, "%.1fK", (double)n / 1e3);
	else {
		snprintf(dst, cap, "%lld", (long long)n);
		return;
	}
	L = strlen(buf);
	if (L >= 3 && buf[L - 3] == '.' && buf[L - 2] == '0' &&
	    (buf[L - 1] == 'K' || buf[L - 1] == 'M' || buf[L - 1] == 'B'))
		memmove(buf + L - 3, buf + L - 1, 2);
	snprintf(dst, cap, "%s", buf);
}

static void xml_escape(const char *in, char *out, size_t cap)
{
	size_t w = 0;

	if (!out || cap < 2)
		return;
	for (; in && *in && w + 6 < cap; in++) {
		if (*in == '&') {
			memcpy(out + w, "&amp;", 5);
			w += 5;
		} else if (*in == '<') {
			memcpy(out + w, "&lt;", 4);
			w += 4;
		} else if (*in == '>') {
			memcpy(out + w, "&gt;", 4);
			w += 4;
		} else
			out[w++] = (char)*in;
	}
	out[w] = '\0';
}

static void fill_usage_based_tooltip(char *tip, size_t tipcap, int markup,
				     const char *accent, double usd,
				     const char *tok_today, int have_cycle,
				     double usd_cycle, const char *tok_cycle,
				     const char *ds, const char *de, int days_left)
{
	const char *dayw = days_left == 1 ? "day left" : "days left";

	if (!markup) {
		if (have_cycle) {
			snprintf(tip, tipcap,
				 "Cursor usage-based: today ~ $%.4f USD "
				 "(local midnight to now)\n"
				 "Today's Tokens: %s\n"
				 "\n"
				 "Billing cycle: ~ $%.4f USD (%s → %s, %d %s)\n"
				 "Tokens this Cycle: %s",
				 usd, tok_today, usd_cycle, ds, de, days_left,
				 dayw, tok_cycle);
		} else {
			snprintf(tip, tipcap,
				 "Cursor usage-based: today ~ $%.4f USD "
				 "(local midnight to now)\n"
				 "Today's Tokens: %s",
				 usd, tok_today);
		}
		return;
	}
	if (have_cycle) {
		snprintf(tip, tipcap,
			 "Cursor usage-based: today ~ $%.4f USD "
			 "(local midnight to now)\n"
			 "<span alpha='0.85'>Today's Tokens:</span> "
			 "<span weight='bold' foreground='%s'>%s</span>\n"
			 "\n"
			 "Billing cycle: ~ $%.4f USD (%s → %s, %d %s)\n"
			 "<span alpha='0.85'>Tokens this Cycle:</span> "
			 "<span weight='bold' foreground='%s'>%s</span>",
			 usd, accent, tok_today, usd_cycle, ds, de, days_left,
			 dayw, accent, tok_cycle);
	} else {
		snprintf(tip, tipcap,
			 "Cursor usage-based: today ~ $%.4f USD "
			 "(local midnight to now)\n"
			 "<span alpha='0.85'>Today's Tokens:</span> "
			 "<span weight='bold' foreground='%s'>%s</span>",
			 usd, accent, tok_today);
	}
}

int main(void)
{
	const char *mock = getenv("CURSOR_WAYBAR_MOCK_JSON");
	if (mock && mock[0]) {
		puts(mock);
		return 0;
	}

	curl_global_init(CURL_GLOBAL_DEFAULT);

	cw_arena arena;
	unsigned char arena_scratch[256 * 1024];
	cw_arena_init(&arena, arena_scratch, sizeof arena_scratch);

	char *token = NULL;
	const char *ev = getenv("CURSOR_SESSION_TOKEN");
	if (!ev)
		ev = getenv("CURSOR_ACCESS_TOKEN");
	if (ev && ev[0])
		token = cw_arena_strdup(&arena, ev);

	char pathbuf[512];
	if (!token) {
		const char *p = getenv("CURSOR_TOKEN_FILE");
		if (p && p[0])
			token = read_file_trim(&arena, p);
		if (!token && default_token_path(pathbuf, sizeof pathbuf))
			token = read_file_trim(&arena, pathbuf);
	}

	if (!token) {
		const char *rs = getenv("CURSOR_READ_SQLITE");
		if (rs && strcmp(rs, "1") == 0) {
			char tbuf[8192];
			if (read_sqlite_access_token(tbuf, sizeof tbuf) == 0)
				token = cw_arena_strdup(&arena, tbuf);
		}
	}

	if (!token || !token[0]) {
		emit_waybar_error(
			"No token: set CURSOR_SESSION_TOKEN, CURSOR_TOKEN_FILE, "
			"~/.config/cursor-waybar/token, or CURSOR_READ_SQLITE=1");
		cw_arena_free(&arena);
		curl_global_cleanup();
		return 0;
	}

	char *user_id = cw_extract_user_id(&arena, token);
	if (!user_id || !user_id[0]) {
		emit_waybar_error("Could not derive user id from token");
		cw_arena_free(&arena);
		curl_global_cleanup();
		return 0;
	}

	char *jwt = cw_jwt_bearer_from_session(&arena, token);
	char *cookie = cw_build_cookie_header(&arena, token, user_id);
	if (!cookie) {
		emit_waybar_error("Cookie build failed");
		cw_arena_free(&arena);
		curl_global_cleanup();
		return 0;
	}

	cJSON *period_root = NULL;
	if (jwt && jwt[0])
		period_root = cw_period_usage_fetch(&arena, jwt);

	cJSON *usage = cw_api_get_usage(&arena, cookie, user_id);
	cJSON *stripe = cw_api_get_stripe(&arena, cookie);

	if (!usage && !stripe) {
		emit_waybar_error(
			"cursor.com/api unreachable or auth failed (HTTP/parse)");
		cw_arena_free(&arena);
		curl_global_cleanup();
		return 0;
	}

	cw_billing_kind_t kind;
	cw_detect_billing(usage, stripe, &kind);

	cJSON *out = cJSON_CreateObject();
	char text[64];
	char tip[2048];
	char tip_combined[8192];

	if ((kind == CW_BILL_PRO || kind == CW_BILL_BUSINESS) && usage) {
		int used = 0, lim = 0;
		if (cw_usage_premium_gpt4(usage, &used, &lim) == 0 &&
		    lim > 0) {
			int pct = (int)((long long)used * 100LL / lim);
			if (pct > 100)
				pct = 100;
			snprintf(text, sizeof text, "Cur %d%%", pct);
			snprintf(tip, sizeof tip,
				 "Cursor premium (gpt-4): %d / %d requests\n"
				 "Billing: %s",
				 used, lim,
				 kind == CW_BILL_BUSINESS ? "business"
							  : "pro");
			cJSON_AddStringToObject(out, "text", text);
			cJSON_AddStringToObject(out, "0", text);
			cJSON_AddStringToObject(out, "tooltip", tip);
			cJSON_AddNumberToObject(out, "percentage", (double)pct);
			cJSON_AddStringToObject(out, "class", usage_class(pct));
		} else {
			kind = CW_BILL_USAGE_BASED;
		}
	}

	if (kind == CW_BILL_USAGE_BASED || !cJSON_GetObjectItem(out, "text")) {
		long long now_ms =
			(long long)time(NULL) * 1000LL;
		long long start_ms = today_start_ms_local();
		long long today_tok = 0;
		double usd = cw_sum_usage_cost_usd_in_range(
			&arena, cookie, start_ms, now_ms, 50, &today_tok);

		int have_cycle = 0;
		double usd_cycle = 0.0;
		long long cycle_tok = 0;
		char ds[40] = "";
		char de[40] = "";
		long long cyc0 = 0, cyc1 = 0;
		if (period_root &&
		    cw_period_billing_cycle_ms(period_root, &cyc0, &cyc1) == 0) {
			long long q0 = cyc0;
			long long q1 = now_ms < cyc1 ? now_ms : cyc1;
			if (q1 > q0) {
				time_t ts_s = (time_t)(q0 / 1000LL);
				time_t ts_e = (time_t)(cyc1 / 1000LL);
				struct tm tms, tme;
				localtime_r(&ts_s, &tms);
				localtime_r(&ts_e, &tme);
				strftime(ds, sizeof ds, "%Y-%m-%d", &tms);
				strftime(de, sizeof de, "%Y-%m-%d", &tme);
				usd_cycle = cw_sum_usage_cost_usd_in_range(
					&arena, cookie, q0, q1, 50, &cycle_tok);
				have_cycle = 1;
			}
		}

		if (usd < 0.01 && usd > 0)
			snprintf(text, sizeof text, "Cur %.2f¢", usd * 100.0);
		else
			snprintf(text, sizeof text, "Cur $%.2f", usd);

		{
			int days_left = 0;
			int show_cycle = have_cycle && ds[0] && de[0];
			char tok_td[24], tok_cy[24] = "";
			int mk = waybar_tooltip_markup();
			const char *accent = tooltip_token_accent();

			fmt_tokens_pretty(tok_td, sizeof tok_td, today_tok);
			if (show_cycle)
				fmt_tokens_pretty(tok_cy, sizeof tok_cy, cycle_tok);
			if (show_cycle && cyc1 > now_ms) {
				long long delta = cyc1 - now_ms;
				days_left = (int)((delta + 86400000LL - 1) /
						   86400000LL);
				if (days_left > 999)
					days_left = 999;
			}
			fill_usage_based_tooltip(tip, sizeof tip, mk, accent, usd,
						   tok_td, show_cycle, usd_cycle,
						   tok_cy, ds, de, days_left);
		}

		if (cJSON_GetObjectItem(out, "text"))
			cJSON_DeleteItemFromObject(out, "text");
		if (cJSON_GetObjectItem(out, "0"))
			cJSON_DeleteItemFromObject(out, "0");
		if (cJSON_GetObjectItem(out, "tooltip"))
			cJSON_DeleteItemFromObject(out, "tooltip");
		if (cJSON_GetObjectItem(out, "percentage"))
			cJSON_DeleteItemFromObject(out, "percentage");
		if (cJSON_GetObjectItem(out, "class"))
			cJSON_DeleteItemFromObject(out, "class");

		cJSON_AddStringToObject(out, "text", text);
		cJSON_AddStringToObject(out, "0", text);
		cJSON_AddStringToObject(out, "tooltip", tip);
		cJSON_AddStringToObject(out, "class", "cursor-usage-usagebased");
	}

	if (period_root) {
		int tp = -1, ap = -1, apii = -1;
		double auto_raw = NAN;
		double api_raw = NAN;
		char period_buf[896];
		if (cw_period_dashboard_metrics(period_root, &tp, &ap, &apii,
						&auto_raw, &api_raw, period_buf,
						sizeof period_buf) == 0) {
			const char *prev = "";
			cJSON *ot = cJSON_GetObjectItem(out, "tooltip");
			int mk = waybar_tooltip_markup();
			char phbuf[4096];
			const char *ph = period_buf;

			if (cJSON_IsString(ot) && ot->valuestring)
				prev = ot->valuestring;
			if (mk) {
				xml_escape(period_buf, phbuf, sizeof phbuf);
				ph = phbuf;
			}
			if (prev[0]) {
				snprintf(tip_combined, sizeof tip_combined,
					 "%s\n---\n%s", ph, prev);
			} else {
				snprintf(tip_combined, sizeof tip_combined, "%s", ph);
			}

			build_auto_api_pill(text, sizeof text, auto_raw, api_raw,
					    tp);

			if (cJSON_GetObjectItem(out, "text"))
				cJSON_DeleteItemFromObject(out, "text");
			if (cJSON_GetObjectItem(out, "0"))
				cJSON_DeleteItemFromObject(out, "0");
			if (cJSON_GetObjectItem(out, "tooltip"))
				cJSON_DeleteItemFromObject(out, "tooltip");
			if (cJSON_GetObjectItem(out, "percentage"))
				cJSON_DeleteItemFromObject(out, "percentage");
			if (cJSON_GetObjectItem(out, "class"))
				cJSON_DeleteItemFromObject(out, "class");

			cJSON_AddStringToObject(out, "text", text);
			cJSON_AddStringToObject(out, "0", text);
			cJSON_AddStringToObject(out, "tooltip", tip_combined);
			if (tp >= 0)
				cJSON_AddNumberToObject(out, "percentage",
							(double)tp);
			cJSON_AddStringToObject(out, "class", plan_usage_class(tp));
		}
		cJSON_Delete(period_root);
		period_root = NULL;
	}

	char *line = cJSON_PrintUnformatted(out);
	cJSON_Delete(out);
	if (line)
		puts(line);
	free(line);

	cJSON_Delete(usage);
	cJSON_Delete(stripe);
	cw_arena_free(&arena);
	curl_global_cleanup();
	return 0;
}
