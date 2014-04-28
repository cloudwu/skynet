#ifndef skynet_malloc_h
#define skynet_malloc_h

#include <stddef.h>

#ifdef NOUSE_JEMALLOC

#define skynet_malloc malloc
#define skynet_calloc calloc
#define skynet_realloc realloc
#define skynet_free free

#endif

void * skynet_malloc(size_t sz);
void * skynet_calloc(size_t nmemb,size_t size);
void * skynet_realloc(void *ptr, size_t size);
void skynet_free(void *ptr);
char * skynet_strdup(const char *str);
void * skynet_lalloc(void *ud, void *ptr, size_t osize, size_t nsize);	// use for lua

#endif
