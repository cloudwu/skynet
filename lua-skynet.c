#include "skynet.h"

#include <lua.h>
#include <lauxlib.h>
#include <stdlib.h>
#include <string.h>

static void
_cb(struct skynet_context * context, void * ud, int session, const char * addr, const void * msg, size_t sz) {
	lua_State *L = ud;
	lua_rawgetp(L, LUA_REGISTRYINDEX, _cb);
	int r;
	if (msg == NULL) {
		lua_pushinteger(L, session);
		r = lua_pcall(L, 1, 0 , 0);
	} else {
		lua_pushinteger(L, session);
		lua_pushstring(L, addr);
		lua_pushlstring(L, msg, sz);
		r = lua_pcall(L, 3, 0 , 0);
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
	const char * parm = NULL;
	int session = 0;
	const char * result;
	int top = lua_gettop(L);
	if (top == 2) {
		if (lua_type(L,2) == LUA_TNUMBER) {
			session = lua_tointeger(L,2);
		} else {
			parm = luaL_checkstring(L,2);
		}
	} else if (top == 3) {
		session = lua_tointeger(L,2);
		parm = luaL_checkstring(L,3);
	} else if (top != 1) {
		luaL_error(L, "skynet.command support only 3 parms (%d)",top);
	}

	result = skynet_command(context, cmd, session, parm);
	if (result) {
		lua_pushstring(L, result);
		return 1;
	}
	return 0;
}

static int
_send(lua_State *L) {
	struct skynet_context * context = lua_touserdata(L, lua_upvalueindex(1));
	int session = -1;
	int index = 0;
	const char * dest = luaL_checkstring(L,1);

	if (lua_type(L,2) == LUA_TNUMBER) {
		session = lua_tointeger(L,2);
		++index;
	}
	int type = lua_type(L,index+2);
	if (type == LUA_TSTRING) {
		size_t len = 0;
		void * msg = (void *)lua_tolstring(L,index+2,&len);
		void * message = malloc(len);
		memcpy(message, msg, len);
		skynet_send(context, dest, session , message, len);
	} else {
		void * msg = lua_touserdata(L,index+2);
		if (msg == NULL) {
			return luaL_error(L, "skynet.send need userdata or string (%s)", lua_typename(L,type));
		}
		int size = luaL_checkinteger(L,index+3);
		skynet_send(context, dest, session, msg, size);
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
