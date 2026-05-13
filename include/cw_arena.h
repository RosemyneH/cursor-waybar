#pragma once

#include <stddef.h>

typedef struct cw_ablock cw_ablock;
struct cw_ablock {
	cw_ablock *next;
	size_t cap;
	size_t pos;
	char *mem;
};

typedef struct cw_arena {
	unsigned char *scratch;
	size_t scratch_cap;
	size_t scratch_used;
	cw_ablock *blocks;
} cw_arena;

void cw_arena_init(cw_arena *a, void *scratch, size_t scratch_cap);
void cw_arena_free(cw_arena *a);
void *cw_arena_alloc(cw_arena *a, size_t size);
char *cw_arena_strdup(cw_arena *a, const char *s);
