#define LUA_LIB

#include <lua.h>
#include <lauxlib.h>

#include "malloc_hook.h"
#include "luashrtbl.h"

static int
ltotal(lua_State *L) {
	size_t t = malloc_used_memory();
	lua_pushinteger(L, (lua_Integer)t);

	return 1;
}

static int
lblock(lua_State *L) {
	size_t t = malloc_memory_block();
	lua_pushinteger(L, (lua_Integer)t);

	return 1;
}

static int
ldumpinfo(lua_State *L) {
	memory_info_dump();

	return 0;
}

static int
ldump(lua_State *L) {
	dump_c_mem();

	return 0;
}

static int
lexpandshrtbl(lua_State *L) {
	int n = luaL_checkinteger(L, 1);
	luaS_expandshr(n);
	return 0;
}

static int
lcurrent(lua_State *L) {
	lua_pushinteger(L, malloc_current_memory());
	return 1;
}

LUAMOD_API int
luaopen_skynet_memory(lua_State *L) {
	luaL_checkversion(L);

	luaL_Reg l[] = {
		{ "total", ltotal },
		{ "block", lblock },
		{ "dumpinfo", ldumpinfo },
		{ "dump", ldump },
		{ "info", dump_mem_lua },
		{ "ssinfo", luaS_shrinfo },
		{ "ssexpand", lexpandshrtbl },
		{ "current", lcurrent },
		{ NULL, NULL },
	};

	luaL_newlib(L,l);

	return 1;
}
