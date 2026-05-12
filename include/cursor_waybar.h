#pragma once

#include "cJSON.h"
#include <stddef.h>

#define CW_API_BASE "https://cursor.com/api"

typedef enum {
	CW_BILL_PRO,
	CW_BILL_BUSINESS,
	CW_BILL_USAGE_BASED,
} cw_billing_kind_t;

int cw_https(const char *method, const char *url, const char *cookie_header,
	     const char *json_body, long *http_code, char **response_body);

int cw_https_bearer_post(const char *url, const char *bearer_token,
			 const char *json_body, long *http_code,
			 char **response_body);

char *cw_url_decode_inplace(char *s);
char *cw_extract_user_id(const char *session_or_jwt);
char *cw_build_cookie_header(const char *session_or_jwt, const char *user_id);

char *cw_jwt_bearer_from_session(const char *session_or_jwt);

cJSON *cw_api_get_usage(const char *cookie_header, const char *user_id);
cJSON *cw_api_get_stripe(const char *cookie_header);
cJSON *cw_api_post_usage_events(const char *cookie_header, long long start_ms,
				long long end_ms);

int cw_usage_premium_gpt4(cJSON *usage_root, int *used_out, int *limit_out);
int cw_detect_billing(cJSON *usage_root, cJSON *stripe_root,
		      cw_billing_kind_t *kind_out);
double cw_sum_today_cost_usd(cJSON *events_response);

cJSON *cw_period_usage_fetch(const char *bearer_jwt);
int cw_period_dashboard_metrics(cJSON *period_root, int *total_pct_out,
				int *auto_pct_out, int *api_pct_out,
				double *auto_raw_out, double *api_raw_out,
				char *tip_append, size_t tip_cap);
