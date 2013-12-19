#ifndef skynet_lua_alloc_h
#define skynet_lua_alloc_h

#include <lua.h>
#include <stddef.h>

struct skynet_lalloc;

struct skynet_lalloc * skynet_lalloc_new(size_t prealloc);
void skynet_lalloc_delete(struct skynet_lalloc *);

void * skynet_lua_alloc(void *ud, void *ptr, size_t osize, size_t nsize);

#endif
