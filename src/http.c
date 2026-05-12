#define _GNU_SOURCE
#include "cursor_waybar.h"

#include <curl/curl.h>
#include <stdlib.h>
#include <string.h>

struct cw_mem {
	char *data;
	size_t len;
};

static size_t cw_write_cb(char *ptr, size_t size, size_t nmemb, void *userdata)
{
	size_t reals = size * nmemb;
	struct cw_mem *m = userdata;
	char *n = realloc(m->data, m->len + reals + 1);
	if (!n)
		return 0;
	m->data = n;
	memcpy(m->data + m->len, ptr, reals);
	m->len += reals;
	m->data[m->len] = '\0';
	return reals;
}

int cw_https(const char *method, const char *url, const char *cookie_header,
	     const char *json_body, long *http_code, char **response_body)
{
	CURL *curl;
	struct cw_mem chunk = {0};
	struct curl_slist *hdrs = NULL;
	long code = 0;
	int ok = -1;

	if (response_body)
		*response_body = NULL;
	if (http_code)
		*http_code = 0;

	curl = curl_easy_init();
	if (!curl)
		return -1;

	hdrs = curl_slist_append(
		hdrs, "User-Agent: Mozilla/5.0 (X11; Linux x86_64) "
		      "AppleWebKit/537.36 (KHTML, like Gecko) "
		      "Chrome/120.0.0.0 Safari/537.36");
	hdrs = curl_slist_append(hdrs, "Accept: application/json");
	hdrs = curl_slist_append(hdrs, "Origin: https://cursor.com");
	hdrs = curl_slist_append(hdrs,
				 "Referer: https://cursor.com/dashboard");

	if (cookie_header) {
		char *cookie_line = NULL;
		if (asprintf(&cookie_line, "Cookie: %s", cookie_header) < 0) {
			curl_slist_free_all(hdrs);
			curl_easy_cleanup(curl);
			return -1;
		}
		hdrs = curl_slist_append(hdrs, cookie_line);
		free(cookie_line);
	}

	if (json_body)
		hdrs = curl_slist_append(hdrs, "Content-Type: application/json");

	curl_easy_setopt(curl, CURLOPT_URL, url);
	curl_easy_setopt(curl, CURLOPT_HTTPHEADER, hdrs);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, cw_write_cb);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &chunk);
	curl_easy_setopt(curl, CURLOPT_TIMEOUT, 20L);
	curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
	curl_easy_setopt(curl, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_1_1);

	if (strcmp(method, "POST") == 0) {
		curl_easy_setopt(curl, CURLOPT_POST, 1L);
		curl_easy_setopt(curl, CURLOPT_POSTFIELDS,
				 json_body ? json_body : "{}");
	} else {
		curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
	}

	if (curl_easy_perform(curl) == CURLE_OK) {
		curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
		if (http_code)
			*http_code = code;
		if (response_body && chunk.data) {
			*response_body = chunk.data;
			chunk.data = NULL;
		}
		ok = 0;
	}

	free(chunk.data);
	curl_slist_free_all(hdrs);
	curl_easy_cleanup(curl);
	return ok;
}
