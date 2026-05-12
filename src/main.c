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

static char *read_file_trim(const char *path)
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
	char *buf = malloc((size_t)n + 1);
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
	char cmd[1100];
	snprintf(cmd, sizeof cmd,
		 "sqlite3 \"%s/.config/Cursor/User/globalStorage/state.vscdb\" "
		 "\"SELECT value FROM ItemTable WHERE key = "
		 "'cursorAuth/accessToken';\" 2>/dev/null",
		 home);
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

int main(void)
{
	const char *mock = getenv("CURSOR_WAYBAR_MOCK_JSON");
	if (mock && mock[0]) {
		puts(mock);
		return 0;
	}

	curl_global_init(CURL_GLOBAL_DEFAULT);

	char *token = NULL;
	const char *ev = getenv("CURSOR_SESSION_TOKEN");
	if (!ev)
		ev = getenv("CURSOR_ACCESS_TOKEN");
	if (ev && ev[0])
		token = strdup(ev);

	char pathbuf[512];
	if (!token) {
		const char *p = getenv("CURSOR_TOKEN_FILE");
		if (p && p[0])
			token = read_file_trim(p);
		if (!token && default_token_path(pathbuf, sizeof pathbuf))
			token = read_file_trim(pathbuf);
	}

	if (!token) {
		const char *rs = getenv("CURSOR_READ_SQLITE");
		if (rs && strcmp(rs, "1") == 0) {
			char tbuf[8192];
			if (read_sqlite_access_token(tbuf, sizeof tbuf) == 0)
				token = strdup(tbuf);
		}
	}

	if (!token || !token[0]) {
		emit_waybar_error(
			"No token: set CURSOR_SESSION_TOKEN, CURSOR_TOKEN_FILE, "
			"~/.config/cursor-waybar/token, or CURSOR_READ_SQLITE=1");
		curl_global_cleanup();
		return 0;
	}

	char *user_id = cw_extract_user_id(token);
	if (!user_id || !user_id[0]) {
		emit_waybar_error("Could not derive user id from token");
		free(token);
		curl_global_cleanup();
		return 0;
	}

	char *jwt = cw_jwt_bearer_from_session(token);
	char *cookie = cw_build_cookie_header(token, user_id);
	free(token);
	token = NULL;
	if (!cookie) {
		free(jwt);
		free(user_id);
		emit_waybar_error("Cookie build failed");
		curl_global_cleanup();
		return 0;
	}

	cJSON *period_root = NULL;
	if (jwt && jwt[0])
		period_root = cw_period_usage_fetch(jwt);
	free(jwt);
	jwt = NULL;

	cJSON *usage = cw_api_get_usage(cookie, user_id);
	cJSON *stripe = cw_api_get_stripe(cookie);

	if (!usage && !stripe) {
		emit_waybar_error(
			"cursor.com/api unreachable or auth failed (HTTP/parse)");
		free(cookie);
		free(user_id);
		curl_global_cleanup();
		return 0;
	}

	cw_billing_kind_t kind;
	cw_detect_billing(usage, stripe, &kind);

	cJSON *out = cJSON_CreateObject();
	char text[64];
	char tip[512];
	char tip_combined[2048];

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
		cJSON *events =
			cw_api_post_usage_events(cookie, start_ms, now_ms);
		double usd = cw_sum_today_cost_usd(events);
		if (usd < 0.01 && usd > 0)
			snprintf(text, sizeof text, "Cur %.2f¢", usd * 100.0);
		else
			snprintf(text, sizeof text, "Cur $%.2f", usd);

		snprintf(tip, sizeof tip,
			 "Cursor usage-based: today ~ $%.4f USD (local midnight "
			 "to now)",
			 usd);

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
		cJSON_Delete(events);
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
			if (cJSON_IsString(ot) && ot->valuestring)
				prev = ot->valuestring;
			if (prev[0])
				snprintf(tip_combined, sizeof tip_combined,
					 "%s\n---\n%s", period_buf, prev);
			else
				snprintf(tip_combined, sizeof tip_combined, "%s",
					 period_buf);

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
			cJSON_AddStringToObject(out, "class", usage_class(tp));
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
	free(cookie);
	free(user_id);
	curl_global_cleanup();
	return 0;
}
