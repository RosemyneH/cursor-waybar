#include "cursor_waybar.h"

#include <ctype.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define CW_FLT_PAGE 100

static int json_double_item(cJSON *n, double *out)
{
	if (!n)
		return -1;
	if (cJSON_IsNumber(n)) {
		*out = cJSON_GetNumberValue(n);
		return 0;
	}
	if (cJSON_IsString(n) && n->valuestring) {
		*out = strtod(n->valuestring, NULL);
		return 0;
	}
	return -1;
}

static int json_ll_item(cJSON *n, long long *out)
{
	if (!n)
		return -1;
	if (cJSON_IsNumber(n)) {
		*out = (long long)cJSON_GetNumberValue(n);
		return 0;
	}
	if (cJSON_IsString(n) && n->valuestring) {
		*out = (long long)strtoll(n->valuestring, NULL, 10);
		return 0;
	}
	return -1;
}

static int json_double_obj(cJSON *o, const char *k, double *out)
{
	return json_double_item(cJSON_GetObjectItemCaseSensitive(o, k), out);
}

static int json_ll_obj(cJSON *o, const char *k, long long *out)
{
	return json_ll_item(cJSON_GetObjectItemCaseSensitive(o, k), out);
}

static int parse_aggregated_usage_response(cJSON *root, double *usd_out,
					   long long *tok_out)
{
	static const char *tot_tok[] = { "totalInputTokens", "totalOutputTokens",
					 "totalCacheWriteTokens",
					 "totalCacheReadTokens" };
	static const char *row_tok[] = { "inputTokens", "outputTokens",
					 "cacheWriteTokens", "cacheReadTokens" };
	double usd = 0.0;
	long long tok = 0LL;
	int ok_usd = 0;
	int ok_tok = 0;
	size_t i;
	double c;
	long long v;

	int n_top_tok = 0;

	if (json_double_obj(root, "totalCostCents", &c) == 0) {
		usd = c / 100.0;
		ok_usd = 1;
	}
	for (i = 0; i < sizeof tot_tok / sizeof tot_tok[0]; i++) {
		if (json_ll_obj(root, tot_tok[i], &v) == 0) {
			tok += v;
			n_top_tok++;
		}
	}
	if (n_top_tok == (int)(sizeof tot_tok / sizeof tot_tok[0]))
		ok_tok = 1;
	else {
		tok = 0LL;
		ok_tok = 0;
	}

	if (!ok_usd || !ok_tok) {
		cJSON *ag =
			cJSON_GetObjectItemCaseSensitive(root, "aggregations");
		if (!cJSON_IsArray(ag)) {
			if (!ok_usd && !ok_tok)
				return -1;
			goto out;
		}
		if (!ok_usd) {
			cJSON *row;
			usd = 0.0;
			cJSON_ArrayForEach(row, ag)
			{
				if (json_double_obj(row, "totalCents", &c) == 0)
					usd += c / 100.0;
			}
			ok_usd = 1;
		}
		if (!ok_tok) {
			cJSON *row;
			tok = 0LL;
			cJSON_ArrayForEach(row, ag)
			{
				for (i = 0; i < sizeof row_tok / sizeof row_tok[0];
				     i++) {
					if (json_ll_obj(row, row_tok[i], &v) == 0)
						tok += v;
				}
			}
			ok_tok = 1;
		}
	}

out:
	if (!ok_usd && !ok_tok)
		return -1;
	if (!ok_usd)
		usd = 0.0;
	if (!ok_tok)
		tok = 0LL;
	*usd_out = usd;
	*tok_out = tok;
	return 0;
}

static cJSON *cw_usage_events_table(const cJSON *root)
{
	static const char *keys[] = { "usageEventsDisplay",
				      "aggregatedUsageEvents",
				      "usageEventsAggregated",
				      "aggregatedEvents" };
	size_t i;

	if (!root)
		return NULL;
	for (i = 0; i < sizeof keys / sizeof keys[0]; i++) {
		cJSON *a =
			cJSON_GetObjectItemCaseSensitive(root, keys[i]);
		if (cJSON_IsArray(a))
			return a;
	}
	return NULL;
}

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

	cJSON *arr = cw_usage_events_table(events_response);
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

long long cw_sum_tokens_from_events_response(cJSON *events_response)
{
	long long sum = 0;
	static const char *keys[] = { "cacheWriteTokens", "cacheReadTokens",
				      "inputTokens", "outputTokens" };
	cJSON *arr;
	cJSON *ev;
	size_t k;

	if (!events_response)
		return 0;
	arr = cw_usage_events_table(events_response);
	if (!cJSON_IsArray(arr))
		return 0;
	cJSON_ArrayForEach(ev, arr)
	{
		cJSON *tu = cJSON_GetObjectItemCaseSensitive(ev, "tokenUsage");
		if (!cJSON_IsObject(tu))
			continue;
		for (k = 0; k < sizeof keys / sizeof keys[0]; k++) {
			cJSON *n = cJSON_GetObjectItemCaseSensitive(tu, keys[k]);
			if (cJSON_IsNumber(n) && !isnan(cJSON_GetNumberValue(n)))
				sum += (long long)cJSON_GetNumberValue(n);
		}
	}
	return sum;
}

static int sum_range_with_aggregated_first(cw_arena *arena,
					   const char *cookie_header,
					   long long start_ms, long long end_ms,
					   double *sum_out, long long *tok_out)
{
	cJSON *j =
		cw_api_post_aggregated_usage_events(arena, cookie_header, start_ms,
						    end_ms);

	if (!j)
		return -1;
	if (parse_aggregated_usage_response(j, sum_out, tok_out) != 0) {
		cJSON_Delete(j);
		return -1;
	}
	cJSON_Delete(j);
	return 0;
}

static double sum_range_filtered_only(cw_arena *arena, const char *cookie_header,
				      long long start_ms, long long end_ms,
				      int max_pages, long long *tokens_out_opt)
{
	double sum = 0.0;
	long long tok = 0LL;
	int p;

	for (p = 1; p <= max_pages; p++) {
		cJSON *ev = cw_api_post_usage_events_page(
			arena, cookie_header, start_ms, end_ms, p, CW_FLT_PAGE);
		cJSON *arr;
		int n;

		if (!ev)
			break;
		arr = cw_usage_events_table(ev);
		n = cJSON_IsArray(arr) ? cJSON_GetArraySize(arr) : 0;
		sum += cw_sum_today_cost_usd(ev);
		tok += cw_sum_tokens_from_events_response(ev);
		cJSON_Delete(ev);
		if (n < CW_FLT_PAGE)
			break;
	}
	if (tokens_out_opt)
		*tokens_out_opt = tok;
	return sum;
}

double cw_sum_usage_cost_usd_in_range(cw_arena *arena, const char *cookie_header,
				      long long start_ms, long long end_ms,
				      int max_pages, long long *tokens_out_opt)
{
	double sum = 0.0;
	long long tok = 0LL;

	if (tokens_out_opt)
		*tokens_out_opt = 0;
	if (!cookie_header || start_ms >= end_ms || max_pages < 1)
		return 0.0;

	if (sum_range_with_aggregated_first(arena, cookie_header, start_ms, end_ms,
					    &sum, &tok) == 0) {
		if (tokens_out_opt)
			*tokens_out_opt = tok;
		return sum;
	}
	return sum_range_filtered_only(arena, cookie_header, start_ms, end_ms,
				       max_pages, tokens_out_opt);
}
