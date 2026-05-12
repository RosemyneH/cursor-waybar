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

cJSON *cw_period_usage_fetch(const char *bearer_jwt)
{
	long code = 0;
	char *body = NULL;
	cJSON *j = NULL;

	if (!bearer_jwt || !bearer_jwt[0])
		return NULL;

	if (cw_https_bearer_post(CW_PERIOD_URL, bearer_jwt, "{}", &code, &body) !=
	    0)
		return NULL;
	if (code != 200 || !body) {
		free(body);
		return NULL;
	}
	j = cJSON_Parse(body);
	free(body);
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
			localtime_r(&ts_s, &tms);
			localtime_r(&ts_e, &tme);
			strftime(ds, sizeof ds, "%Y-%m-%d", &tms);
			strftime(de, sizeof de, "%Y-%m-%d", &tme);
			append_fmt(tip_append, tip_cap, &w,
				   "Billing cycle: %s -> %s\n", ds, de);
		}
	}

	append_fmt(tip_append, tip_cap, &w, "Total (plan): %d%%\n",
		   *total_pct_out);
	if (*auto_pct_out >= 0)
		append_fmt(tip_append, tip_cap, &w,
			   "Auto + Composer: %d%%\n", *auto_pct_out);
	if (*api_pct_out >= 0)
		append_fmt(tip_append, tip_cap, &w, "API: %d%%\n",
			   *api_pct_out);

	{
		cJSON *dm = cJSON_GetObjectItemCaseSensitive(period_root,
							     "displayMessage");
		if (cJSON_IsString(dm) && dm->valuestring &&
		    dm->valuestring[0])
			append_fmt(tip_append, tip_cap, &w, "%s\n",
				   dm->valuestring);
	}

	return 0;
}
