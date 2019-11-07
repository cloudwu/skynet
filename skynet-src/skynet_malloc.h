#ifndef skynet_malloc_h
#define skynet_malloc_h

#include <stddef.h>

#define skynet_malloc(sz) skynet_malloc_tag(__LINE__, sz)
#define skynet_malloc_raw malloc
#define skynet_calloc(nb, size) skynet_calloc_tag(__LINE__, nb, size)
#define skynet_calloc_raw calloc
#define skynet_realloc(ptr, size) skynet_realloc_tag(__LINE__, ptr, size)
#define skynet_realloc_raw realloc
#define skynet_free free
#define skynet_memalign memalign
#define skynet_aligned_alloc aligned_alloc
#define skynet_posix_memalign posix_memalign

void * skynet_malloc_tag(unsigned tag, size_t sz);
void * skynet_calloc_tag(unsigned tag, size_t nmemb,size_t size);
void * skynet_realloc_tag(unsigned tag, void *ptr, size_t size);
void skynet_free(void *ptr);
char * skynet_strdup(const char *str);
void * skynet_lalloc(void *ptr, size_t osize, size_t nsize);	// use for lua
void * codecache_lalloc(void* ud, void *ptr, size_t osize, size_t nsize);	// use for lua
void * skynet_memalign(size_t alignment, size_t size);
void * skynet_aligned_alloc(size_t alignment, size_t size);
int skynet_posix_memalign(void **memptr, size_t alignment, size_t size);

#endif
