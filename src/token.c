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

static int b64url_decode(cw_arena *arena, const char *in, unsigned char **out,
			size_t *out_len)
{
	size_t inlen = strlen(in);
	size_t pad = (4 - (inlen % 4)) % 4;
	char *buf = cw_arena_alloc(arena, inlen + pad + 1);
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
	unsigned char *dec = cw_arena_alloc(arena, oalloc + 1);
	if (!dec)
		return -1;

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
	*out = dec;
	*out_len = j;
	return 0;
}

static char *sub_user_from_jwt(cw_arena *arena, const char *jwt)
{
	const char *p1 = strchr(jwt, '.');
	if (!p1)
		return NULL;
	const char *p2 = strchr(p1 + 1, '.');
	if (!p2)
		return NULL;
	size_t plen = (size_t)(p2 - p1 - 1);
	char *payload = cw_arena_alloc(arena, plen + 1);
	if (!payload)
		return NULL;
	memcpy(payload, p1 + 1, plen);
	payload[plen] = '\0';

	unsigned char *raw = NULL;
	size_t rawlen = 0;
	if (b64url_decode(arena, payload, &raw, &rawlen) != 0)
		return NULL;

	cJSON *root = cJSON_ParseWithLength((const char *)raw, rawlen);
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
		id = cw_arena_alloc(arena, (size_t)(e - u) + 1);
		if (id) {
			memcpy(id, u, (size_t)(e - u));
			id[e - u] = '\0';
		}
	}
	cJSON_Delete(root);
	return id;
}

char *cw_extract_user_id(cw_arena *arena, const char *session_or_jwt)
{
	char *copy = cw_arena_strdup(arena, session_or_jwt);
	if (!copy)
		return NULL;
	cw_url_decode_inplace(copy);

	char *sep = strstr(copy, "::");
	if (sep) {
		*sep = '\0';
		return cw_arena_strdup(arena, copy);
	}

	return sub_user_from_jwt(arena, copy);
}

char *cw_build_cookie_header(cw_arena *arena, const char *session_or_jwt,
			     const char *user_id)
{
	char *work = cw_arena_strdup(arena, session_or_jwt);
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
			size_t need = strlen(user_id) + strlen(work) + 16;
			char *combined = cw_arena_alloc(arena, need);
			if (!combined)
				return NULL;
			snprintf(combined, need, "%s%%3A%%3A%s", user_id, work);
			work = combined;
		}
	}

	size_t need = strlen(work) + 40;
	char *hdr = cw_arena_alloc(arena, need);
	if (!hdr)
		return NULL;
	snprintf(hdr, need, "WorkosCursorSessionToken=%s", work);
	return hdr;
}

char *cw_jwt_bearer_from_session(cw_arena *arena, const char *session_or_jwt)
{
	char *copy = cw_arena_strdup(arena, session_or_jwt);
	if (!copy)
		return NULL;
	cw_url_decode_inplace(copy);
	char *sep = strstr(copy, "::");
	if (sep)
		return cw_arena_strdup(arena, sep + 2);
	return cw_arena_strdup(arena, copy);
}
