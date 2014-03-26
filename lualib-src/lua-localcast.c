#include <lua.h>
#include <lauxlib.h>
#include "localcast.h"

#include <stdlib.h>
#include <stdint.h>
#include <string.h>

/*
	table handles
	string msg
	  lightuserdata ptr
	  integer sz
 */
static int
_pack_message(lua_State *L) {
	luaL_checktype(L,1,LUA_TTABLE);
	int type = lua_type(L,2);
	void * msg = NULL;
	size_t sz = 0;
	switch(type) {
	case LUA_TSTRING: {
		const char * str = lua_tolstring(L,2,&sz);
		msg = malloc(sz);
		memcpy(msg, str, sz);
		break;
	}
	case LUA_TLIGHTUSERDATA:
		msg = lua_touserdata(L,2);
		sz = luaL_checkinteger(L,3);
		break;
	default:
		luaL_error(L, "type error : %s", lua_typename(L,type));
		break;
	}
	struct localcast *lc = malloc(sizeof(struct localcast));
	lc->n = lua_rawlen(L,1);
	uint32_t *group = malloc(lc->n * sizeof(uint32_t));
	int i;
	for (i=0;i<lc->n;i++) {
		lua_rawgeti(L,1,i+1);
		group[i] = lua_tounsigned(L,-1);
		lua_pop(L,1);
	}
	lc->msg = msg;
	lc->sz = sz;
	lc->group = group;
	lua_pushlightuserdata(L,lc);
	lua_pushinteger(L, sizeof(*lc));
	return 2;
};

int
luaopen_mcast_c(lua_State *L) {
	luaL_checkversion(L);
	lua_pushcfunction(L, _pack_message);

	return 1;
}
