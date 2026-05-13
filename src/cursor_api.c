#include "cursor_waybar.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

cJSON *cw_api_get_usage(cw_arena *arena, const char *cookie_header,
			const char *user_id)
{
	char url[768];
	long code = 0;
	char *body = NULL;
	cJSON *j = NULL;

	if (snprintf(url, sizeof url, "%s/usage?user=%s", CW_API_BASE,
		     user_id) >= (int)sizeof url)
		return NULL;

	if (cw_https("GET", url, cookie_header, NULL, &code, &body, arena) != 0)
		return NULL;
	if (code != 200 || !body)
		return NULL;
	j = cJSON_Parse(body);
	return j;
}

cJSON *cw_api_get_stripe(cw_arena *arena, const char *cookie_header)
{
	char url[256];
	long code = 0;
	char *body = NULL;
	cJSON *j = NULL;

	snprintf(url, sizeof url, "%s/auth/stripe", CW_API_BASE);

	if (cw_https("GET", url, cookie_header, NULL, &code, &body, arena) != 0)
		return NULL;
	if (code != 200 || !body)
		return NULL;
	j = cJSON_Parse(body);
	return j;
}

static char *cw_build_usage_events_range_json(long long start_ms,
					      long long end_ms, int page,
					      int page_size)
{
	cJSON *req = cJSON_CreateObject();
	char *post = NULL;

	if (!req)
		return NULL;
	if (page < 1)
		page = 1;
	if (page_size < 1)
		page_size = 1;
	if (page_size > 500)
		page_size = 500;

	cJSON_AddNumberToObject(req, "teamId", 0);
	{
		char s[32], e[32];
		snprintf(s, sizeof s, "%lld", (long long)start_ms);
		snprintf(e, sizeof e, "%lld", (long long)end_ms);
		cJSON_AddStringToObject(req, "startDate", s);
		cJSON_AddStringToObject(req, "endDate", e);
	}
	cJSON_AddNumberToObject(req, "page", page);
	cJSON_AddNumberToObject(req, "pageSize", page_size);

	post = cJSON_PrintUnformatted(req);
	cJSON_Delete(req);
	return post;
}

static cJSON *cw_post_usage_events_at(cw_arena *arena, const char *cookie_header,
				      const char *path, long long start_ms,
				      long long end_ms, int page, int page_size)
{
	char url[320];
	long code = 0;
	char *body = NULL;
	char *post =
		cw_build_usage_events_range_json(start_ms, end_ms, page, page_size);
	cJSON *resp = NULL;

	if (!post)
		return NULL;
	if (snprintf(url, sizeof url, "%s%s", CW_API_BASE, path) >=
	    (int)sizeof url) {
		free(post);
		return NULL;
	}

	if (cw_https("POST", url, cookie_header, post, &code, &body, arena) != 0) {
		free(post);
		return NULL;
	}
	free(post);

	if (code != 200 || !body)
		return NULL;
	resp = cJSON_Parse(body);
	return resp;
}

cJSON *cw_api_post_usage_events_page(cw_arena *arena, const char *cookie_header,
				     long long start_ms, long long end_ms,
				     int page, int page_size)
{
	return cw_post_usage_events_at(
		arena, cookie_header, "/dashboard/get-filtered-usage-events",
		start_ms, end_ms, page, page_size);
}

cJSON *cw_api_post_aggregated_usage_events_page(cw_arena *arena,
						const char *cookie_header,
						long long start_ms, long long end_ms,
						int page, int page_size)
{
	return cw_post_usage_events_at(
		arena, cookie_header, "/dashboard/get-aggregated-usage-events",
		start_ms, end_ms, page, page_size);
}

cJSON *cw_api_post_aggregated_usage_events(cw_arena *arena,
					  const char *cookie_header,
					  long long start_ms, long long end_ms)
{
	char url[256];
	long code = 0;
	char *body = NULL;
	cJSON *req = cJSON_CreateObject();
	cJSON *resp = NULL;
	char *post = NULL;

	if (!req)
		return NULL;
	cJSON_AddNumberToObject(req, "teamId", -1);
	cJSON_AddNumberToObject(req, "startDate", (double)start_ms);
	cJSON_AddNumberToObject(req, "endDate", (double)end_ms);
	post = cJSON_PrintUnformatted(req);
	cJSON_Delete(req);
	if (!post)
		return NULL;

	snprintf(url, sizeof url, "%s/dashboard/get-aggregated-usage-events",
		 CW_API_BASE);

	if (cw_https("POST", url, cookie_header, post, &code, &body, arena) != 0) {
		free(post);
		return NULL;
	}
	free(post);

	if (code != 200 || !body)
		return NULL;
	resp = cJSON_Parse(body);
	return resp;
}

cJSON *cw_api_post_usage_events(cw_arena *arena, const char *cookie_header,
				long long start_ms, long long end_ms)
{
	return cw_api_post_usage_events_page(arena, cookie_header, start_ms,
					     end_ms, 1, 100);
}
