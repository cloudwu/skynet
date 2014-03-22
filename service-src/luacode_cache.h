#ifndef lua_code_cache_h
#define lua_code_cache_h

#include <lua.h>
#include <stddef.h>

int luacode_loadfile(lua_State *L, const char *filename);
int luacode_lib(lua_State *);

#endif
