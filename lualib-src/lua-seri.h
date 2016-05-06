#ifndef LUA_SERIALIZE_H
#define LUA_SERIALIZE_H

#include <lua.h>

int luaseri_pack(lua_State *L);
int luaseri_unpack(lua_State *L);

#endif
