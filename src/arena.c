#include "cw_arena.h"

#include <stdlib.h>
#include <string.h>

enum { cw_min_block = 65536u };

static size_t cw_align_up(size_t n)
{
	enum { a = sizeof(void *) };
	return (n + a - 1u) & ~(a - 1u);
}

void cw_arena_init(cw_arena *a, void *scratch, size_t scratch_cap)
{
	a->scratch = scratch;
	a->scratch_cap = scratch_cap;
	a->scratch_used = 0;
	a->blocks = NULL;
}

void cw_arena_free(cw_arena *a)
{
	cw_ablock *b = a->blocks;
	while (b) {
		cw_ablock *n = b->next;
		free(b->mem);
		free(b);
		b = n;
	}
	a->blocks = NULL;
	a->scratch_used = 0;
}

void *cw_arena_alloc(cw_arena *a, size_t size)
{
	size_t n = cw_align_up(size);
	if (n == 0)
		n = cw_align_up(1);

	if (a->scratch && a->scratch_used + n <= a->scratch_cap) {
		void *p = a->scratch + a->scratch_used;
		a->scratch_used += n;
		return p;
	}

	cw_ablock *u = a->blocks;
	if (u && u->pos + n <= u->cap) {
		void *p = u->mem + u->pos;
		u->pos += n;
		return p;
	}

	size_t cap = n > cw_min_block ? n : cw_min_block;
	char *mem = malloc(cap);
	if (!mem)
		return NULL;
	cw_ablock *b = malloc(sizeof *b);
	if (!b) {
		free(mem);
		return NULL;
	}
	b->mem = mem;
	b->cap = cap;
	b->pos = n;
	b->next = a->blocks;
	a->blocks = b;
	return mem;
}

char *cw_arena_strdup(cw_arena *a, const char *s)
{
	size_t n = strlen(s) + 1;
	char *p = cw_arena_alloc(a, n);
	if (p)
		memcpy(p, s, n);
	return p;
}
