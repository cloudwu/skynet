#ifndef skynet_malloc_h
#define skynet_malloc_h

#include <stddef.h>

// 关于这个文件可能在看的时候, 会有疑问, 那就是为什么这里定义了 malloc 但是却没有报告重定义的错误.
// 具体的细节可以看: http://www.blogbus.com/bigwhite-logs/77791357.html

// 以下函数的定义如果使用了 jemalloc 的话, 那么会在 malloc_hook.c 文件中找到函数的定义.
// 如果没有使用 jemalloc 的话, 那么使用的就是标准库的 malloc, 这里只是又做了一次声明而已.

#define skynet_malloc malloc
#define skynet_calloc calloc
#define skynet_realloc realloc
#define skynet_free free

void * skynet_malloc(size_t sz);
void * skynet_calloc(size_t nmemb,size_t size);
void * skynet_realloc(void *ptr, size_t size);
void skynet_free(void *ptr);
char * skynet_strdup(const char *str);
void * skynet_lalloc(void *ud, void *ptr, size_t osize, size_t nsize);	// use for lua

#endif
