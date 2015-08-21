#ifndef LUA_SHORT_STRING_TABLE_H
#define LUA_SHORT_STRING_TABLE_H

#ifndef DISABLE_SHORT_STRING

#include "lstring.h"

#else

static inline int luaS_shrinfo(lua_State *L) { return 0; }
static inline void luaS_initshr() {}
static inline void luaS_exitshr() {}
static inline void luaS_expandshr(int n);

#endif

#endif
