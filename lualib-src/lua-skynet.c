#include "skynet.h"
#include "lua-seri.h"

#include <lua.h>
#include <lauxlib.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

static int
_cb(struct skynet_context * context, void * ud, int session, const char * addr, const void * msg, size_t sz) {
	lua_State *L = ud;
	int trace = 1;
	int top = lua_gettop(L);
	if (top == 1) {
		lua_rawgetp(L, LUA_REGISTRYINDEX, _cb);
	} else {
		assert(top == 2);
		lua_pushvalue(L,2);
	}

	int r;
	if (msg == NULL) {
		if (addr == NULL) {
			lua_pushinteger(L, session);
			r = lua_pcall(L, 1, 0 , trace);
		} else {
			lua_pushinteger(L, session);
			lua_pushstring(L, addr);
			r = lua_pcall(L, 2, 0 , trace);
		}
	} else {
		lua_pushinteger(L, session);
		lua_pushstring(L, addr);
		lua_pushlightuserdata(L,(void *)msg);
		lua_pushinteger(L,sz);
		r = lua_pcall(L, 4, 0 , trace);
	}
	if (r == LUA_OK) 
		return 0;
	const char * self = skynet_command(context, "REG", NULL);
	switch (r) {
	case LUA_ERRRUN:
		skynet_error(context, "lua call [%s to %s : %d msgsz = %d] error : %s", addr , self, session, sz, lua_tostring(L,-1));
		break;
	case LUA_ERRMEM:
		skynet_error(context, "lua memory error : [%s to %s : %d]", addr , self, session);
		break;
	case LUA_ERRERR:
		skynet_error(context, "lua error in error : [%s to %s : %d]", addr , self, session);
		break;
	case LUA_ERRGCMM:
		skynet_error(context, "lua gc error : [%s to %s : %d]", addr , self, session);
		break;
	};

	lua_pop(L,1);

	return 0;
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
	const char * parm = NULL;
	if (lua_gettop(L) == 2) {
		parm = luaL_checkstring(L,2);
	}

	result = skynet_command(context, cmd, parm);
	if (result) {
		lua_pushstring(L, result);
		return 1;
	}
	return 0;
}

static int
_genid(lua_State *L) {
	struct skynet_context * context = lua_touserdata(L, lua_upvalueindex(1));
	int session = skynet_send(context, NULL, NULL, -1, NULL, 0 , 0);
	lua_pushinteger(L, session);
	return 1;
}

static int
_send(lua_State *L) {
	struct skynet_context * context = lua_touserdata(L, lua_upvalueindex(1));
	int session = 0;
	int index = 0;
	const char * dest = luaL_checkstring(L,1);

	if (lua_type(L,2) == LUA_TNUMBER) {
		session = lua_tointeger(L,2);
		++index;
	}
	if (lua_gettop(L) == index + 1) {
		session = skynet_send(context, NULL, dest, session , NULL, 0, 0);
	} else {
		int type = lua_type(L,index+2);
		if (type == LUA_TSTRING) {
			size_t len = 0;
			void * msg = (void *)lua_tolstring(L,index+2,&len);
			session = skynet_send(context, NULL, dest, session , msg, len, 0);
		} else if (type == LUA_TNIL) {
			session = skynet_send(context, NULL, dest, session , NULL, 0, 0);
		} else {
			luaL_checktype(L,index+2, LUA_TLIGHTUSERDATA);
			void * msg = lua_touserdata(L,index+2);
			int size = luaL_checkinteger(L,index+3);
			session = skynet_send(context, NULL, dest, session, msg, size, DONTCOPY);
		}
	}
	if (session < 0) {
		return luaL_error(L, "skynet.send drop the message to %s", dest);
	}
	lua_pushinteger(L,session);
	return 1;
}

static int
_redirect(lua_State *L) {
	struct skynet_context * context = lua_touserdata(L, lua_upvalueindex(1));
	const char * dest = luaL_checkstring(L,1);
	const char * source = luaL_checkstring(L,2);
	int session = luaL_checkinteger(L,3);

	if (lua_gettop(L) == 3) {
		session = skynet_send(context, source, dest, session , NULL, 0, 0);
	} else {
		int type = lua_type(L,4);
		if (type == LUA_TSTRING) {
			size_t len = 0;
			void * msg = (void *)lua_tolstring(L,4,&len);
			skynet_send(context, source, dest, session , msg, len, 0);
		} else if (type == LUA_TNIL) {
			session = skynet_send(context, source, dest, session , NULL, 0, 0);
		} else {
			luaL_checktype(L, 4, LUA_TLIGHTUSERDATA);
			void * msg = lua_touserdata(L,4);
			int size = luaL_checkinteger(L,5);
			skynet_send(context, source, dest, session, msg, size, DONTCOPY);
		}
	}

	return 0;
}

static int
_error(lua_State *L) {
	struct skynet_context * context = lua_touserdata(L, lua_upvalueindex(1));
	skynet_error(context, "%s", luaL_checkstring(L,1));
	return 0;
}

static int
_tostring(lua_State *L) {
	if (lua_isnoneornil(L,1)) {
		return 0;
	}
	char * msg = lua_touserdata(L,1);
	int sz = luaL_checkinteger(L,2);
	lua_pushlstring(L,msg,sz);
	return 1;
}

// define in lua-remoteobj.c
int remoteobj_init(lua_State *L);

int
luaopen_skynet_c(lua_State *L) {
	luaL_checkversion(L);
	
	luaL_Reg pack[] = {
		{ "pack", _luaseri_pack },
		{ "unpack", _luaseri_unpack },
		{ NULL, NULL },
	};

	luaL_Reg l[] = {
		{ "send" , _send },
		{ "genid", _genid },
		{ "redirect", _redirect },
		{ "command" , _command },
		{ "callback" , _callback },
		{ "error", _error },
		{ "tostring", _tostring },
		{ NULL, NULL },
	};

	lua_createtable(L, 0, (sizeof(pack) + sizeof(l))/sizeof(luaL_Reg)-2);
	lua_newtable(L);
	lua_pushstring(L,"__remote");
	luaL_setfuncs(L,pack,2);

	lua_getfield(L, LUA_REGISTRYINDEX, "skynet_context");
	struct skynet_context * ctx = lua_touserdata(L,-1);
	if (ctx == NULL) {
		return luaL_error(L, "Init skynet context first");
	}

	luaL_setfuncs(L,l,1);

	lua_pushcfunction(L, remoteobj_init);
	lua_setfield(L, -2, "remote_init");

	return 1;
}
