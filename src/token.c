#define _GNU_SOURCE
#include "cursor_waybar.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

char *cw_url_decode_inplace(char *s)
{
	char *w = s;
	for (char *r = s; *r;) {
		if (*r == '%' && isxdigit((unsigned char)r[1]) &&
		    isxdigit((unsigned char)r[2])) {
			char hex[3] = {r[1], r[2], 0};
			*w++ = (char)strtol(hex, NULL, 16);
			r += 3;
		} else {
			*w++ = *r++;
		}
	}
	*w = '\0';
	return s;
}

static size_t b64_value(unsigned char c)
{
	if (c >= 'A' && c <= 'Z')
		return (size_t)(c - 'A');
	if (c >= 'a' && c <= 'z')
		return (size_t)(c - 'a' + 26);
	if (c >= '0' && c <= '9')
		return (size_t)(c - '0' + 52);
	if (c == '+')
		return 62;
	if (c == '/')
		return 63;
	return 64;
}

static int b64url_decode(const char *in, unsigned char **out, size_t *out_len)
{
	size_t inlen = strlen(in);
	size_t pad = (4 - (inlen % 4)) % 4;
	char *buf = malloc(inlen + pad + 1);
	if (!buf)
		return -1;
	memcpy(buf, in, inlen);
	for (size_t i = 0; i < pad; i++)
		buf[inlen++] = '=';
	buf[inlen] = '\0';

	for (size_t i = 0; i < inlen; i++) {
		if (buf[i] == '-')
			buf[i] = '+';
		else if (buf[i] == '_')
			buf[i] = '/';
	}

	size_t oalloc = (inlen / 4) * 3;
	unsigned char *dec = malloc(oalloc + 1);
	if (!dec) {
		free(buf);
		return -1;
	}

	size_t j = 0;
	for (size_t i = 0; i + 4 <= inlen; i += 4) {
		size_t v0 = b64_value((unsigned char)buf[i]);
		size_t v1 = b64_value((unsigned char)buf[i + 1]);
		size_t v2 = b64_value((unsigned char)buf[i + 2]);
		size_t v3 = b64_value((unsigned char)buf[i + 3]);
		if (v0 > 63 || v1 > 63)
			break;
		dec[j++] = (unsigned char)((v0 << 2) | (v1 >> 4));
		if (buf[i + 2] != '=') {
			if (v2 > 63)
				break;
			dec[j++] = (unsigned char)(((v1 & 15) << 4) | (v2 >> 2));
		}
		if (buf[i + 3] != '=') {
			if (v3 > 63)
				break;
			dec[j++] = (unsigned char)(((v2 & 3) << 6) | v3);
		}
	}
	dec[j] = '\0';
	free(buf);
	*out = dec;
	*out_len = j;
	return 0;
}

static char *sub_user_from_jwt(const char *jwt)
{
	const char *p1 = strchr(jwt, '.');
	if (!p1)
		return NULL;
	const char *p2 = strchr(p1 + 1, '.');
	if (!p2)
		return NULL;
	size_t plen = (size_t)(p2 - p1 - 1);
	char *payload = malloc(plen + 1);
	if (!payload)
		return NULL;
	memcpy(payload, p1 + 1, plen);
	payload[plen] = '\0';

	unsigned char *raw = NULL;
	size_t rawlen = 0;
	if (b64url_decode(payload, &raw, &rawlen) != 0) {
		free(payload);
		return NULL;
	}
	free(payload);

	cJSON *root = cJSON_ParseWithLength((const char *)raw, rawlen);
	free(raw);
	if (!root)
		return NULL;

	cJSON *sub = cJSON_GetObjectItemCaseSensitive(root, "sub");
	if (!cJSON_IsString(sub) || !sub->valuestring) {
		cJSON_Delete(root);
		return NULL;
	}

	const char *s = sub->valuestring;
	const char *u = strstr(s, "user_");
	char *id = NULL;
	if (u) {
		const char *e = u + 5;
		while (*e && (isalnum((unsigned char)*e) || *e == '_'))
			e++;
		id = malloc((size_t)(e - u) + 1);
		if (id) {
			memcpy(id, u, (size_t)(e - u));
			id[e - u] = '\0';
		}
	}
	cJSON_Delete(root);
	return id;
}

char *cw_extract_user_id(const char *session_or_jwt)
{
	char *copy = strdup(session_or_jwt);
	if (!copy)
		return NULL;
	cw_url_decode_inplace(copy);

	char *sep = strstr(copy, "::");
	if (sep) {
		*sep = '\0';
		char *id = strdup(copy);
		free(copy);
		return id;
	}

	char *from_jwt = sub_user_from_jwt(copy);
	free(copy);
	return from_jwt;
}

char *cw_build_cookie_header(const char *session_or_jwt, const char *user_id)
{
	char *work = strdup(session_or_jwt);
	if (!work)
		return NULL;

	if (strstr(work, "::") && !strstr(work, "%3A%3A")) {
		char *r = work;
		while ((r = strstr(r, "::")) != NULL) {
			memmove(r + 6, r + 2, strlen(r + 2) + 1);
			memcpy(r, "%3A%3A", 6);
			r += 6;
		}
	}

	if (!strstr(work, "%3A%3A") && !strstr(work, "::")) {
		if (user_id && user_id[0]) {
			char *combined = NULL;
			if (asprintf(&combined, "%s%%3A%%3A%s", user_id, work) <
			    0) {
				free(work);
				return NULL;
			}
			free(work);
			work = combined;
		}
	}

	char *hdr = NULL;
	if (asprintf(&hdr, "WorkosCursorSessionToken=%s", work) < 0)
		hdr = NULL;
	free(work);
	return hdr;
}

char *cw_jwt_bearer_from_session(const char *session_or_jwt)
{
	char *copy = strdup(session_or_jwt);
	if (!copy)
		return NULL;
	cw_url_decode_inplace(copy);
	char *sep = strstr(copy, "::");
	if (sep) {
		char *jwt = strdup(sep + 2);
		free(copy);
		return jwt;
	}
	char *jwt = strdup(copy);
	free(copy);
	return jwt;
}
