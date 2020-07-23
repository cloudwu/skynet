#define LUA_LIB

#include <lua.h>
#include <lauxlib.h>

#include "malloc_hook.h"

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
	const char *opts = NULL;
	if (lua_isstring(L, 1)) {
		opts = luaL_checkstring(L,1);
	}
	memory_info_dump(opts);

	return 0;
}

static int
ljestat(lua_State *L) {
	static const char* names[] = {
		"stats.allocated",
		"stats.resident",
		"stats.retained",
		"stats.mapped",
		"stats.active" };
	static size_t flush = 1;
	mallctl_int64("epoch", &flush); // refresh je.stats.cache
	lua_newtable(L);
	int i;
	for (i = 0; i < (sizeof(names)/sizeof(names[0])); i++) {
		lua_pushstring(L, names[i]);
		lua_pushinteger(L,  (lua_Integer) mallctl_int64(names[i], NULL));
		lua_settable(L, -3);
	}
	return 1;
}

static int
lmallctl(lua_State *L) {
	const char *name = luaL_checkstring(L,1);
	lua_pushinteger(L, (lua_Integer) mallctl_int64(name, NULL));
	return 1;
}

static int
ldump(lua_State *L) {
	dump_c_mem();

	return 0;
}

static int
lcurrent(lua_State *L) {
	lua_pushinteger(L, malloc_current_memory());
	return 1;
}

static int
ldumpheap(lua_State *L) {
	mallctl_cmd("prof.dump");
	return 0;
}

static int
lprofactive(lua_State *L) {
	bool *pval, active;
	if (lua_isnone(L, 1)) {
		pval = NULL;
	} else {
		active = lua_toboolean(L, 1) ? true : false;
		pval = &active;
	}
	bool ret = mallctl_bool("prof.active", pval);
	lua_pushboolean(L, ret);
	return 1;
}

LUAMOD_API int
luaopen_skynet_memory(lua_State *L) {
	luaL_checkversion(L);

	luaL_Reg l[] = {
		{ "total", ltotal },
		{ "block", lblock },
		{ "dumpinfo", ldumpinfo },
		{ "jestat", ljestat },
		{ "mallctl", lmallctl },
		{ "dump", ldump },
		{ "info", dump_mem_lua },
		{ "current", lcurrent },
		{ "dumpheap", ldumpheap },
		{ "profactive", lprofactive },
		{ NULL, NULL },
	};

	luaL_newlib(L,l);

	return 1;
}
