#include "skynet.h"

#include <lua.h>
#include <lauxlib.h>
#include <stdlib.h>
#include <string.h>

static void
_cb(struct skynet_context * context, void * ud, const char * addr, const void * msg, size_t sz_session) {
	lua_State *L = ud;
	lua_rawgetp(L, LUA_REGISTRYINDEX, _cb);
	int r;
	if (msg == NULL) {
		lua_pushinteger(L, (int)sz_session);
		r = lua_pcall(L, 1, 0 , 0);
	} else {
		lua_pushstring(L, addr);
		lua_pushlstring(L, msg, sz_session);
		r = lua_pcall(L, 2, 0 , 0);
	}
	if (r == LUA_OK) 
		return;
	skynet_error(context, "lua error %s", lua_tostring(L,-1));
}

static int
_callback(lua_State *L) {
	struct skynet_context * context = lua_touserdata(L, lua_upvalueindex(1));
	if (context == NULL) {
		return luaL_error(L, "Init skynet context first");
	}

	luaL_checktype(L,1,LUA_TFUNCTION);
	lua_settop(L,1);
	lua_rawsetp(L, LUA_REGISTRYINDEX, _cb);
	lua_rawgeti(L, LUA_REGISTRYINDEX, LUA_RIDX_MAINTHREAD);
	lua_State *gL = lua_tothread(L,-1);

	skynet_callback(context, gL, _cb);

	return 0;
}

static int
_command(lua_State *L) {
	struct skynet_context * context = lua_touserdata(L, lua_upvalueindex(1));
	const char * cmd = luaL_checkstring(L,1);
	const char * result;
	if (lua_gettop(L) == 2) {
		const char * parm = luaL_checkstring(L,2);
		result = skynet_command(context, cmd, parm);
	} else {
		result = skynet_command(context, cmd, NULL);
	}
	if (result) {
		lua_pushstring(L, result);
		return 1;
	}
	return 0;
}

static int
_send(lua_State *L) {
	struct skynet_context * context = lua_touserdata(L, lua_upvalueindex(1));
	const char * dest = luaL_checkstring(L,1);
	int type = lua_type(L,2);
	if (type == LUA_TSTRING) {
		size_t len = 0;
		void * msg = (void *)lua_tolstring(L,2,&len);
		void * message = malloc(len);
		memcpy(message, msg, len);
		skynet_send(context, dest, message, len);
	} else {
		void * msg = lua_touserdata(L,2);
		if (msg == NULL) {
			return luaL_error(L, "skynet.send need userdata or string (%s)", lua_typename(L,type));
		}
		int size = luaL_checkinteger(L,3);
		skynet_send(context, dest, msg, size);
	}
	return 0;
}

static int
_error(lua_State *L) {
	struct skynet_context * context = lua_touserdata(L, lua_upvalueindex(1));
	skynet_error(context, "%s", luaL_checkstring(L,1));
	return 0;
}

int
luaopen_skynet(lua_State *L) {
	luaL_checkversion(L);
	luaL_Reg l[] = {
		{ "send" , _send },
		{ "command" , _command },
		{ "callback" , _callback },
		{ "error", _error },
		{ NULL, NULL },
	};

	luaL_newlibtable(L,l);

	lua_getfield(L, LUA_REGISTRYINDEX, "skynet_context");
	struct skynet_context * ctx = lua_touserdata(L,-1);
	if (ctx == NULL) {
		return luaL_error(L, "Init skynet context first");
	}

	luaL_setfuncs(L,l,1);

	return 1;
}
