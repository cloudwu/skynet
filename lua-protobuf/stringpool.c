#include "alloc.h"

#include <stdlib.h>
#include <string.h>

#define PAGE_SIZE 256

struct _stringpool {
	char * buffer;
	size_t len;
	struct _stringpool *next;
};

struct _stringpool * 
_pbcS_new(void) {
	struct _stringpool * ret = (struct _stringpool *)malloc(sizeof(struct _stringpool) + PAGE_SIZE);
	ret->buffer = (char *)(ret + 1);
	ret->len = 0;
	ret->next = NULL;
	return ret;
}

void 
_pbcS_delete(struct _stringpool *pool) {
	while(pool) {
		struct _stringpool *next = pool->next;
		free(pool);
		pool = next;
	}
}

const char *
_pbcS_build(struct _stringpool *pool, const char * str , int sz) {
	size_t s = sz + 1;
	if (s < PAGE_SIZE - pool->len) {
		char * ret = pool->buffer + pool->len;
		memcpy(pool->buffer + pool->len, str, s);
		pool->len += s;
		return ret;
	}
	if (s > PAGE_SIZE) {
		struct _stringpool * next = (struct _stringpool *)malloc(sizeof(struct _stringpool) + s);
		next->buffer = (char *)(next + 1);
		memcpy(next->buffer, str, s);
		next->len = s;
		next->next = pool->next;
		pool->next = next;
		return next->buffer;
	}
	struct _stringpool *next = (struct _stringpool *)malloc(sizeof(struct _stringpool) + PAGE_SIZE);
	next->buffer = pool->buffer;
	next->next = pool->next;
	next->len = pool->len;

	pool->next = next;
	pool->buffer = (char *)(next + 1);
	memcpy(pool->buffer, str, s);
	pool->len = s;
	return pool->buffer;
}
