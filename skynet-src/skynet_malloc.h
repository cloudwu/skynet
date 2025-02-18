#ifndef skynet_malloc_h
#define skynet_malloc_h

#include <stddef.h>
#include <stdlib.h>

void * skynet_malloc(size_t sz);
void * skynet_calloc(size_t nmemb,size_t size);
void * skynet_realloc(void *ptr, size_t size);
void skynet_free(void *ptr);
char * skynet_strdup(const char *str);
void * skynet_lalloc(void *ptr, size_t osize, size_t nsize);	// use for lua
void * skynet_memalign(size_t alignment, size_t size);
void * skynet_aligned_alloc(size_t alignment, size_t size);
int skynet_posix_memalign(void **memptr, size_t alignment, size_t size);

#ifdef __APPLE__
    #include <dlfcn.h>
    #define DYLD_INTERPOSE(_replacement,_replacee) \
    __attribute__((used)) static struct{ const void* replacement; const void* replacee; } _interpose_##_replacee \
                __attribute__ ((section ("__DATA,__interpose,interposing"))) = { (const void*)(unsigned long)&_replacement, (const void*)(unsigned long)&_replacee }

    DYLD_INTERPOSE(skynet_malloc, malloc);
    DYLD_INTERPOSE(skynet_calloc, calloc);
    DYLD_INTERPOSE(skynet_realloc, realloc);
    DYLD_INTERPOSE(skynet_free, free);
    // MacOS 下面没有 memalign 函数
    // DYLD_INTERPOSE(skynet_memalign, memalign);
    DYLD_INTERPOSE(skynet_aligned_alloc, aligned_alloc);
    DYLD_INTERPOSE(skynet_posix_memalign, posix_memalign);
#else
    #define skynet_malloc malloc
    #define skynet_calloc calloc
    #define skynet_realloc realloc
    #define skynet_free free
    #define skynet_memalign memalign
    #define skynet_aligned_alloc aligned_alloc
    #define skynet_posix_memalign posix_memalign
#endif

#endif
