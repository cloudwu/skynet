#ifndef skynet_malloc_h
#define skynet_malloc_h

#include <stddef.h>

#define malloc skynet_malloc
#define calloc skynet_calloc
#define realloc skynet_realloc
#define free skynet_free

void * skynet_malloc(size_t sz);
void * skynet_calloc(size_t nmemb,size_t size);
void * skynet_realloc(void *ptr, size_t size);
void skynet_free(void *ptr);
char * skynet_strdup(const char *str);
void * skynet_lalloc(void *ud, void *ptr, size_t osize, size_t nsize);	// use for lua

#endif
