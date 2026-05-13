#define _POSIX_C_SOURCE 200809L
#include "cursor_waybar.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define CW_PERIOD_URL                                                          \
	"https://api2.cursor.sh/aiserver.v1.DashboardService/GetCurrentPeriodUsage"

cJSON *cw_period_usage_fetch(cw_arena *arena, const char *bearer_jwt)
{
	long code = 0;
	char *body = NULL;
	cJSON *j = NULL;

	if (!bearer_jwt || !bearer_jwt[0])
		return NULL;

	if (cw_https_bearer_post(CW_PERIOD_URL, bearer_jwt, "{}", &code, &body,
				 arena) != 0)
		return NULL;
	if (code != 200 || !body)
		return NULL;
	j = cJSON_Parse(body);
	return j;
}

static int pct_round_clamp(double x)
{
	if (!isfinite(x))
		return -1;
	int v = (int)(x + 0.5);
	if (v < 0)
		v = 0;
	if (v > 100)
		v = 100;
	return v;
}

static void append_fmt(char *buf, size_t cap, int *woff, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	int w = *woff;
	if (w < 0 || (size_t)w >= cap)
		w = (int)cap - 1;
	int n = vsnprintf(buf + w, cap - (size_t)w, fmt, ap);
	va_end(ap);
	if (n > 0 && w + n < (int)cap)
		*woff = w + n;
	else if (n > 0)
		*woff = (int)cap - 1;
}

int cw_period_dashboard_metrics(cJSON *period_root, int *total_pct_out,
				int *auto_pct_out, int *api_pct_out,
				double *auto_raw_out, double *api_raw_out,
				char *tip_append, size_t tip_cap)
{
	cJSON *en = NULL;
	cJSON *pu = NULL;
	double total = NAN, auto_pct = NAN, api_pct = NAN;
	int w = 0;

	if (!period_root || !total_pct_out || !auto_pct_out || !api_pct_out ||
	    !tip_append || tip_cap < 32)
		return -1;

	*total_pct_out = *auto_pct_out = *api_pct_out = -1;
	if (auto_raw_out)
		*auto_raw_out = NAN;
	if (api_raw_out)
		*api_raw_out = NAN;
	tip_append[0] = '\0';

	en = cJSON_GetObjectItemCaseSensitive(period_root, "enabled");
	if (cJSON_IsBool(en) && cJSON_IsFalse(en))
		return -1;

	pu = cJSON_GetObjectItemCaseSensitive(period_root, "planUsage");
	if (!cJSON_IsObject(pu))
		return -1;

	{
		cJSON *t =
			cJSON_GetObjectItemCaseSensitive(pu, "totalPercentUsed");
		cJSON *a =
			cJSON_GetObjectItemCaseSensitive(pu, "autoPercentUsed");
		cJSON *p =
			cJSON_GetObjectItemCaseSensitive(pu, "apiPercentUsed");
		if (cJSON_IsNumber(t))
			total = cJSON_GetNumberValue(t);
		if (cJSON_IsNumber(a))
			auto_pct = cJSON_GetNumberValue(a);
		if (cJSON_IsNumber(p))
			api_pct = cJSON_GetNumberValue(p);
	}

	if (!isfinite(total)) {
		cJSON *lim =
			cJSON_GetObjectItemCaseSensitive(pu, "limit");
		cJSON *inc =
			cJSON_GetObjectItemCaseSensitive(pu, "includedSpend");
		if (cJSON_IsNumber(lim) && cJSON_GetNumberValue(lim) > 0.0 &&
		    cJSON_IsNumber(inc))
			total = cJSON_GetNumberValue(inc) /
				cJSON_GetNumberValue(lim) * 100.0;
	}

	*total_pct_out = pct_round_clamp(total);
	if (*total_pct_out < 0)
		return -1;

	*auto_pct_out = pct_round_clamp(auto_pct);
	*api_pct_out = pct_round_clamp(api_pct);

	if (auto_raw_out && isfinite(auto_pct))
		*auto_raw_out = auto_pct;
	if (api_raw_out && isfinite(api_pct))
		*api_raw_out = api_pct;

	{
		cJSON *bs =
			cJSON_GetObjectItemCaseSensitive(period_root,
							 "billingCycleStart");
		cJSON *be =
			cJSON_GetObjectItemCaseSensitive(period_root,
							 "billingCycleEnd");
		if (cJSON_IsString(bs) && bs->valuestring &&
		    cJSON_IsString(be) && be->valuestring) {
			long long ms_s = atoll(bs->valuestring);
			long long ms_e = atoll(be->valuestring);
			time_t ts_s = (time_t)(ms_s / 1000LL);
			time_t ts_e = (time_t)(ms_e / 1000LL);
			struct tm tms, tme;
			char ds[32], de[32];
			long long now_ms = (long long)time(NULL) * 1000LL;
			int days_left = 0;
			if (ms_e > now_ms) {
				long long delta = ms_e - now_ms;
				days_left = (int)((delta + 86400000LL - 1) /
						  86400000LL);
				if (days_left > 999)
					days_left = 999;
			}
			localtime_r(&ts_s, &tms);
			localtime_r(&ts_e, &tme);
			strftime(ds, sizeof ds, "%Y-%m-%d", &tms);
			strftime(de, sizeof de, "%Y-%m-%d", &tme);
			append_fmt(tip_append, tip_cap, &w,
				   "Billing cycle: %s -> %s (%d %s)\r", ds, de,
				   days_left,
				   days_left == 1 ? "day left" : "days left");
		}
	}

	append_fmt(tip_append, tip_cap, &w, "Total (plan): %.1f%%\r", total);
	if (isfinite(auto_pct))
		append_fmt(tip_append, tip_cap, &w,
			   "Auto + Composer: %.1f%%\r", auto_pct);
	if (isfinite(api_pct))
		append_fmt(tip_append, tip_cap, &w, "API: %.1f%%\r",
			   api_pct);

	{
		const char *show_dm = getenv("CURSOR_WAYBAR_DISPLAY_MESSAGE");
		cJSON *dm = cJSON_GetObjectItemCaseSensitive(period_root,
							     "displayMessage");
		if (show_dm && strcmp(show_dm, "1") == 0 &&
		    cJSON_IsString(dm) && dm->valuestring &&
		    dm->valuestring[0])
			append_fmt(tip_append, tip_cap, &w, "%s\r",
				   dm->valuestring);
	}

	return 0;
}

int cw_period_billing_cycle_ms(cJSON *period_root, long long *start_ms_out,
			       long long *end_ms_out)
{
	cJSON *bs;
	cJSON *be;

	if (!period_root || !start_ms_out || !end_ms_out)
		return -1;
	bs = cJSON_GetObjectItemCaseSensitive(period_root, "billingCycleStart");
	be = cJSON_GetObjectItemCaseSensitive(period_root, "billingCycleEnd");
	if (!cJSON_IsString(bs) || !bs->valuestring ||
	    !cJSON_IsString(be) || !be->valuestring)
		return -1;
	*start_ms_out = atoll(bs->valuestring);
	*end_ms_out = atoll(be->valuestring);
	if (*start_ms_out <= 0LL || *end_ms_out <= *start_ms_out)
		return -1;
	return 0;
}
