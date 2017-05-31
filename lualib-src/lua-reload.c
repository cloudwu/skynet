#include "skynet.h"

#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>

#include <assert.h>

// LUA_CACHELIB may defined in patched lua for shared proto
#ifdef LUA_CACHELIB

#define codecache luaopen_cache

#else

static int
cleardummy(lua_State *L) {
  return 0;
}

static int 
codecache(lua_State *L) {
	luaL_Reg l[] = {
		{ "clear", cleardummy },
		{ "mode", cleardummy },
		{ NULL, NULL },
	};
	luaL_newlib(L,l);
	lua_getglobal(L, "loadfile");
	lua_setfield(L, -2, "loadfile");
	return 1;
}

#endif

static int 
traceback (lua_State *L) {
	const char *msg = lua_tostring(L, 1);
	if (msg)
		luaL_traceback(L, L, msg, 1);
	else {
		lua_pushliteral(L, "(no error message)");
	}
	return 1;
}

static const char *
optstring(struct skynet_context *ctx, const char *key, const char * str) {
	const char * ret = skynet_command(ctx, "GETENV", key);
	if (ret == NULL) {
		return str;
	}
	return ret;
}

static struct lua_State *
load_service(struct skynet_context *ctx, const char * args, size_t sz, lua_State *oL) {
	void *ud = NULL;
	lua_Alloc alloc = lua_getallocf(oL, &ud);
	lua_State *L = lua_newstate(alloc, ud);
	lua_gc(L, LUA_GCSTOP, 0);
	lua_pushboolean(L, 1);  /* signal for libraries to ignore env. vars. */
	lua_setfield(L, LUA_REGISTRYINDEX, "LUA_NOENV");
	luaL_openlibs(L);
	lua_pushlightuserdata(L, ctx);
	lua_setfield(L, LUA_REGISTRYINDEX, "skynet_context");
	luaL_requiref(L, "skynet.codecache", codecache , 0);
	lua_getfield(L, -1, "mode");
	if (lua_type(L, -1) == LUA_TFUNCTION) {
		lua_pushstring(L, "OFF");
		lua_pcall(L, 1, 0, 0);
	} else {
		lua_pop(L, 1);
	}

	lua_pop(L,1);

	const char *path = optstring(ctx, "lua_path","./lualib/?.lua;./lualib/?/init.lua");
	lua_pushstring(L, path);
	lua_setglobal(L, "LUA_PATH");
	const char *cpath = optstring(ctx, "lua_cpath","./luaclib/?.so");
	lua_pushstring(L, cpath);
	lua_setglobal(L, "LUA_CPATH");
	const char *service = optstring(ctx, "luaservice", "./service/?.lua");
	lua_pushstring(L, service);
	lua_setglobal(L, "LUA_SERVICE");
	const char *preload = skynet_command(ctx, "GETENV", "preload");
	lua_pushstring(L, preload);
	lua_setglobal(L, "LUA_PRELOAD");

	lua_pushcfunction(L, traceback);
	assert(lua_gettop(L) == 1);

	const char * loader = optstring(ctx, "lualoader", "./lualib/loader.lua");

	int r = luaL_loadfile(L,loader);
	if (r != LUA_OK) {
		skynet_error(ctx, "Can't load %s : %s", loader, lua_tostring(L, -1));
		lua_close(L);
		return NULL;
	}
	lua_pushlstring(L, args, sz);
	r = lua_pcall(L,1,0,1);
	if (r != LUA_OK) {
		skynet_error(ctx, "lua loader error : %s", lua_tostring(L, -1));
		lua_close(L);
		// NOTICE: If the script change the skynet callback, it may cause the core dump.
		return NULL;
	}
	lua_settop(L,0);

	lua_gc(L, LUA_GCRESTART, 0);

	return L;
}

static int
lreload(lua_State *L) {
	size_t sz;
	const char * args = luaL_checklstring(L, 1, &sz);
	lua_getfield(L, LUA_REGISTRYINDEX, "skynet_context");
	luaL_checktype(L, -1, LUA_TLIGHTUSERDATA);
	struct skynet_context *ctx = lua_touserdata(L, -1);
	lua_pop(L, 1);
	struct lua_State *nL = load_service(ctx, args, (int)sz, L);
	if (nL == NULL) {
		return luaL_error(L, "load [%s] failed", args);
	}
	lua_pushlightuserdata(L, nL);

	return 1;
}

static int
deleteL(lua_State *L) {
	lua_rawgeti(L, 1, 1);
	lua_State *nL = lua_touserdata(L, -1);
	if (nL) {
		lua_close(nL);
		lua_pushnil(L);
		lua_rawseti(L, 1, 1);
	}
	return 0;
}

static void
clear_parent(lua_State *L) {
	lua_getfield(L, LUA_REGISTRYINDEX, "skynet_parent");
	lua_State *oL = lua_touserdata(L, -1);
	lua_pop(L, 1);
	if (oL == NULL) {
		return;
	}

	lua_getfield(oL, LUA_REGISTRYINDEX, "skynet_parent");
	lua_State *gL = lua_touserdata(oL, -1);
	lua_pop(oL, 1);
	if (gL == NULL) {
		return;
	}
	
	lua_getfield(oL, LUA_REGISTRYINDEX, "skynet_child");	// oL[skynet_child][1] = nil
	lua_pushnil(oL);	// don't gc me
	lua_rawseti(oL, -2, 1);
	lua_pop(oL, 1);

	lua_getfield(gL, LUA_REGISTRYINDEX, "skynet_child");	// gL[skynet_child][1] = L
	lua_pushlightuserdata(gL, L);
	lua_rawseti(gL, -2, 1);
	lua_pop(gL, 1);

	lua_pushlightuserdata(L, gL);
	lua_setfield(L, LUA_REGISTRYINDEX, "skynet_parent");

	lua_close(oL);
}

static int
llink(lua_State *L) {
	luaL_checktype(L, 1, LUA_TLIGHTUSERDATA);	// new L
	lua_State *nL = lua_touserdata(L, 1);

	lua_rawgeti(L, LUA_REGISTRYINDEX, LUA_RIDX_MAINTHREAD);
	lua_State *mL = lua_tothread(L, -1);
	lua_pop(L, 1);
	if (mL) {
		lua_pushlightuserdata(nL, mL);
		lua_setfield(nL, LUA_REGISTRYINDEX, "skynet_parent");
	}

	lua_createtable(L, 1, 0);
	lua_pushlightuserdata(L, nL);
	lua_rawseti(L, -2, 1);

	lua_createtable(L, 0, 1);
	lua_pushcfunction(L, deleteL);
	lua_setfield(L, -2, "__gc");

	lua_setmetatable(L, -2);

	lua_setfield(L, LUA_REGISTRYINDEX, "skynet_child");

	clear_parent(L);
	
	return 0;
}

int
luaopen_reload_core(lua_State *L) {
	luaL_checkversion(L);

	luaL_Reg l[] = {
		{ "reload", lreload },
		{ "link", llink },
		{ NULL, NULL },
	};

	luaL_newlib(L,l);
	return 1;
}
