#define LUA_LIB

#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>

#include "lstring.h"
#include "lobject.h"
#include "ltable.h"
#include "lstate.h"
#include "lapi.h"
#include "lgc.h"

#ifdef ENABLE_SHORT_STRING_TABLE

static void
mark_shared(lua_State *L) {
	if (lua_type(L, -1) != LUA_TTABLE) {
		luaL_error(L, "Not a table, it's a %s.", lua_typename(L, lua_type(L, -1)));
	}
	Table * t = (Table *)lua_topointer(L, -1);
	if (isshared(t))
		return;
	makeshared(t);
	luaL_checkstack(L, 4, NULL);
	if (lua_getmetatable(L, -1)) {
		luaL_error(L, "Can't share metatable");
	}
	lua_pushnil(L);
	while (lua_next(L, -2) != 0) {
		int i;
		for (i=0;i<2;i++) {
			int idx = -i-1;
			int t = lua_type(L, idx);
			switch (t) {
			case LUA_TTABLE:
				mark_shared(L);
				break;
			case LUA_TNUMBER:
			case LUA_TBOOLEAN:
			case LUA_TLIGHTUSERDATA:
				break;
			case LUA_TFUNCTION:
				if (!lua_iscfunction(L, idx) || lua_getupvalue(L, idx, 1) != NULL)
					luaL_error(L, "Invalid function");
				break;
			case LUA_TSTRING: {
				const char *str = lua_tostring(L, idx);
				TString *ts = (TString *)(str - sizeof(UTString));
				if(ts->tt == LUA_TLNGSTR)
					makeshared(ts);
				else
					luaS_fix(G(L), ts);
				break;
			}
			default:
				luaL_error(L, "Invalid type [%s]", lua_typename(L, t));
				break;
			}
		}
		lua_pop(L, 1);
	}
}

static int
make_matrix(lua_State *L) {
	// turn off gc , because marking shared will prevent gc mark.
	lua_gc(L, LUA_GCSTOP, 0);
	mark_shared(L);
	Table * t = (Table *)lua_topointer(L, -1);
	lua_pushlightuserdata(L, t);
	return 1;
}

static int
clone_table(lua_State *L) {
	Table * t = (Table *)lua_touserdata(L, 1);
	if (!isshared(t))
		return luaL_error(L, "Not a shared table");

	sethvalue(L, L->top, t);
	api_incr_top(L);

	return 1;
}

struct state_ud {
	lua_State *L;
};

static int
close_state(lua_State *L) {
	struct state_ud *ud = (struct state_ud *)luaL_checkudata(L, 1, "BOXMATRIXSTATE");
	if (ud->L) {
		lua_close(ud->L);
		ud->L = NULL;
	}
	return 0;
}

static int
get_matrix(lua_State *L) {
	struct state_ud *ud = (struct state_ud *)luaL_checkudata(L, 1, "BOXMATRIXSTATE");
	if (ud->L) {
		const void * v = lua_topointer(ud->L, 1);
		lua_pushlightuserdata(L, (void *)v);
		return 1;
	}
	return 0;
}

static int
get_size(lua_State *L) {
	struct state_ud *ud = (struct state_ud *)luaL_checkudata(L, 1, "BOXMATRIXSTATE");
	if (ud->L) {
		lua_Integer sz = lua_gc(ud->L, LUA_GCCOUNT, 0);
		sz *= 1024;
		sz += lua_gc(ud->L, LUA_GCCOUNTB, 0);
		lua_pushinteger(L, sz);
	} else {
		lua_pushinteger(L, 0);
	}
	return 1;
}


static int
box_state(lua_State *L, lua_State *mL) {
	struct state_ud *ud = (struct state_ud *)lua_newuserdata(L, sizeof(*ud));
	ud->L = mL;
	if (luaL_newmetatable(L, "BOXMATRIXSTATE")) {
		lua_pushvalue(L, -1);
		lua_setfield(L, -2, "__index");
		lua_pushcfunction(L, close_state);
		lua_setfield(L, -2, "close");
		lua_pushcfunction(L, get_matrix);
		lua_setfield(L, -2, "getptr");
		lua_pushcfunction(L, get_size);
		lua_setfield(L, -2, "size");
	}
	lua_setmetatable(L, -2);

	return 1;
}

static int
load_matrixfile(lua_State *L) {
	luaL_openlibs(L);
	const char * source = (const char *)lua_touserdata(L, 1);
	if (source[0] == '@') {
		if (luaL_loadfilex_(L, source+1, NULL) != LUA_OK)
			lua_error(L);
	} else {
		if (luaL_loadstring(L, source) != LUA_OK)
			lua_error(L);
	}
	lua_replace(L, 1);
	if (lua_pcall(L, lua_gettop(L) - 1, 1, 0) != LUA_OK)
		lua_error(L);
	lua_gc(L, LUA_GCCOLLECT, 0);
	lua_pushcfunction(L, make_matrix);
	lua_insert(L, -2);
	lua_call(L, 1, 1);
	return 1;
}

static int
matrix_from_file(lua_State *L) {
	lua_State *mL = luaL_newstate();
	if (mL == NULL) {
		return luaL_error(L, "luaL_newstate failed");
	}
	const char * source = luaL_checkstring(L, 1);
	int top = lua_gettop(L);
	lua_pushcfunction(mL, load_matrixfile);
	lua_pushlightuserdata(mL, (void *)source);
	if (top > 1) {
		if (!lua_checkstack(mL, top + 1)) {
			return luaL_error(L, "Too many argument %d", top);
		}
		int i;
		for (i=2;i<=top;i++) {
			switch(lua_type(L, i)) {
			case LUA_TBOOLEAN:
				lua_pushboolean(mL, lua_toboolean(L, i));
				break;
			case LUA_TNUMBER:
				if (lua_isinteger(L, i)) {
					lua_pushinteger(mL, lua_tointeger(L, i));
				} else {
					lua_pushnumber(mL, lua_tonumber(L, i));
				}
				break;
			case LUA_TLIGHTUSERDATA:
				lua_pushlightuserdata(mL, lua_touserdata(L, i));
				break;
			case LUA_TFUNCTION:
				if (lua_iscfunction(L, i) && lua_getupvalue(L, i, 1) == NULL) {
					lua_pushcfunction(mL, lua_tocfunction(L, i));
					break;
				}
				return luaL_argerror(L, i, "Only support light C function");
			default:
				return luaL_argerror(L, i, "Type invalid");
			}
		}
	}
	int ok = lua_pcall(mL, top, 1, 0);
	if (ok != LUA_OK) {
		lua_pushstring(L, lua_tostring(mL, -1));
		lua_close(mL);
		lua_error(L);
	}
	return box_state(L, mL);
}

LUAMOD_API int
luaopen_skynet_sharetable_core(lua_State *L) {
	luaL_checkversion(L);
	luaL_Reg l[] = {
		{ "clone", clone_table },
		{ "matrix", matrix_from_file },
		{ NULL, NULL },
	};
	luaL_newlib(L, l);
	return 1;
}

#else

LUAMOD_API int
luaopen_skynet_sharetable_core(lua_State *L) {
	return luaL_error(L, "No share string table support");
}

#endif