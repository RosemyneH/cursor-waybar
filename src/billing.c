#include "cursor_waybar.h"

#include <ctype.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

static int lower_eq(const char *a, const char *b)
{
	while (*a && *b) {
		if (tolower((unsigned char)*a) != tolower((unsigned char)*b))
			return 0;
		a++;
		b++;
	}
	return *a == *b;
}

int cw_usage_premium_gpt4(cJSON *usage_root, int *used_out, int *limit_out)
{
	cJSON *gpt4 = cJSON_GetObjectItemCaseSensitive(usage_root, "gpt-4");
	if (!gpt4 || !cJSON_IsObject(gpt4))
		return -1;

	cJSON *nu = cJSON_GetObjectItemCaseSensitive(gpt4, "numRequests");
	cJSON *mx = cJSON_GetObjectItemCaseSensitive(gpt4, "maxRequestUsage");

	if (!cJSON_IsNumber(nu))
		return -1;

	*used_out = (int)cJSON_GetNumberValue(nu);
	if (cJSON_IsNumber(mx) && !isnan(cJSON_GetNumberValue(mx)))
		*limit_out = (int)cJSON_GetNumberValue(mx);
	else
		*limit_out = -1;
	return 0;
}

int cw_detect_billing(cJSON *usage_root, cJSON *stripe_root,
		      cw_billing_kind_t *kind_out)
{
	int used = 0, lim = -1;
	int has_limit = 0;

	if (usage_root &&
	    cw_usage_premium_gpt4(usage_root, &used, &lim) == 0 &&
	    lim >= 0)
		has_limit = 1;

	if (!has_limit) {
		*kind_out = CW_BILL_USAGE_BASED;
		return 0;
	}

	if (!stripe_root) {
		*kind_out = CW_BILL_PRO;
		return 0;
	}

	cJSON *is_team =
		cJSON_GetObjectItemCaseSensitive(stripe_root, "isTeamMember");
	if (!cJSON_IsBool(is_team) || !cJSON_IsTrue(is_team)) {
		*kind_out = CW_BILL_PRO;
		return 0;
	}

	const char *member = "";
	const char *team_type = "";
	cJSON *mt = cJSON_GetObjectItemCaseSensitive(stripe_root,
						     "membershipType");
	cJSON *tt = cJSON_GetObjectItemCaseSensitive(stripe_root,
						     "teamMembershipType");
	if (cJSON_IsString(mt) && mt->valuestring)
		member = mt->valuestring;
	if (cJSON_IsString(tt) && tt->valuestring)
		team_type = tt->valuestring;

	if (lower_eq(member, "enterprise") || lower_eq(member, "business") ||
	    lower_eq(team_type, "business") ||
	    lower_eq(team_type, "self_serve"))
		*kind_out = CW_BILL_BUSINESS;
	else
		*kind_out = CW_BILL_PRO;

	return 0;
}

double cw_sum_today_cost_usd(cJSON *events_response)
{
	double sum = 0.0;
	if (!events_response)
		return 0.0;

	cJSON *arr = cJSON_GetObjectItemCaseSensitive(events_response,
						      "usageEventsDisplay");
	if (!cJSON_IsArray(arr))
		return 0.0;

	cJSON *ev = NULL;
	cJSON_ArrayForEach(ev, arr)
	{
		cJSON *tu = cJSON_GetObjectItemCaseSensitive(ev, "tokenUsage");
		if (cJSON_IsObject(tu)) {
			cJSON *tc =
				cJSON_GetObjectItemCaseSensitive(tu, "totalCents");
			if (cJSON_IsNumber(tc))
				sum += cJSON_GetNumberValue(tc) / 100.0;
		}
	}
	return sum;
}
