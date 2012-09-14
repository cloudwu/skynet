#include "skynet.h"
#include "lua-seri.h"

#include "luacompat52.h"

#include <lua.h>
#include <lauxlib.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <time.h>

#define NANOSEC 1000000000

struct stat {
	lua_State *L;
	int count;
	uint32_t ti_sec;
	uint32_t ti_nsec;
};

static void
_stat_begin(struct stat *S, struct timespec *ti) {
	S->count++;
	clock_gettime(CLOCK_THREAD_CPUTIME_ID, ti);
}

static void
_stat_end(struct stat *S, struct timespec *ti) {
	struct timespec end;
	clock_gettime(CLOCK_THREAD_CPUTIME_ID, &end);
	int diffsec = end.tv_sec - ti->tv_sec;
	assert(diffsec>=0);
	int diffnsec = end.tv_nsec - ti->tv_nsec;
	if (diffnsec < 0) {
		--diffsec;
		diffnsec += NANOSEC;
	}
	S->ti_nsec += diffnsec;
	if (S->ti_nsec > NANOSEC) {
		++S->ti_sec;
		S->ti_nsec -= NANOSEC;
	}
	S->ti_sec += diffsec;
}

static int
_stat(lua_State *L) {
	lua_rawgetp(L, LUA_REGISTRYINDEX, _stat);
	struct stat *S = lua_touserdata(L,-1);
	if (S==NULL) {
		luaL_error(L, "set callback first");
	}
	const char * what = luaL_checkstring(L,1);
	if (strcmp(what,"count")==0) {
		lua_pushinteger(L, S->count);
		return 1;
	}
	if (strcmp(what,"time")==0) {
		double t = (double)S->ti_sec + (double)S->ti_nsec / NANOSEC;
		lua_pushnumber(L, t);
		return 1;
	}
	return 0;
}

static int
_cb(struct skynet_context * context, void * ud, int type, int session, uint32_t source, const void * msg, size_t sz) {
	struct stat *S = ud;
	lua_State *L = S->L;
	struct timespec ti;
	_stat_begin(S, &ti);
	int trace = 1;
	int top = lua_gettop(L);
	if (top == 1) {
		lua_rawgetp(L, LUA_REGISTRYINDEX, _cb);
	} else {
		assert(top == 2);
		lua_pushvalue(L,2);
	}

	lua_pushinteger(L, type);
	lua_pushlightuserdata(L, (void *)msg);
	lua_pushinteger(L,sz);
	lua_pushinteger(L, session);
	lua_pushnumber(L, source);

	int r = lua_pcall(L, 5, 0 , trace);

	_stat_end(S, &ti);
	if (r == LUA_OK) 
		return 0;
	const char * self = skynet_command(context, "REG", NULL);
	switch (r) {
	case LUA_ERRRUN:
		skynet_error(context, "lua call [%x to %s : %d msgsz = %d] error : %s", source , self, session, sz, lua_tostring(L,-1));
		break;
	case LUA_ERRMEM:
		skynet_error(context, "lua memory error : [%x to %s : %d]", source , self, session);
		break;
	case LUA_ERRERR:
		skynet_error(context, "lua error in error : [%x to %s : %d]", source , self, session);
		break;
	case LUA_ERRGCMM:
		skynet_error(context, "lua gc error : [%x to %s : %d]", source , self, session);
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

	struct stat * S = lua_newuserdata(L, sizeof(*S));
	memset(S, 0, sizeof(*S));
	S->L = gL;

	lua_rawsetp(L, LUA_REGISTRYINDEX, _stat);

	skynet_callback(context, S, _cb);

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
	int session = skynet_send(context, 0, 0, PTYPE_TAG_ALLOCSESSION , 0 , NULL, 0);
	lua_pushinteger(L, session);
	return 1;
}

// copy from _send

static int
_sendname(lua_State *L, struct skynet_context * context, const char * dest) {
	int type = luaL_checkinteger(L, 2);
	int session = 0;
	if (lua_isnil(L,3)) {
		type |= PTYPE_TAG_ALLOCSESSION;
	} else {
		session = luaL_checkinteger(L,3);
	}

	int mtype = lua_type(L,4);
	switch (mtype) {
	case LUA_TSTRING: {
		size_t len = 0;
		void * msg = (void *)lua_tolstring(L,4,&len);
		session = skynet_sendname(context, dest, type, session , msg, len);
		break;
	}
	case LUA_TNIL :
		session = skynet_sendname(context, dest, type, session , NULL, 0);
		break;
	case LUA_TLIGHTUSERDATA: {
		luaL_checktype(L, 4, LUA_TLIGHTUSERDATA);
		void * msg = lua_touserdata(L,4);
		int size = luaL_checkinteger(L,5);
		session = skynet_sendname(context, dest, type | PTYPE_TAG_DONTCOPY, session, msg, size);
		break;
	}
	default:
		luaL_error(L, "skynet.send invalid param %s", lua_type(L,4));
	}
	lua_pushinteger(L,session);
	return 1;
}

/*
	unsigned address
	 string address
	integer type
	integer session
	string message
	 lightuserdata message_ptr
	 integer len
 */
static int
_send(lua_State *L) {
	struct skynet_context * context = lua_touserdata(L, lua_upvalueindex(1));
	int addr_type = lua_type(L,1);
	uint32_t dest = 0;
	switch(addr_type) {
	case LUA_TNUMBER:
		dest = lua_tounsigned(L,1);
		break;
	case LUA_TSTRING: {
		const char * addrname = lua_tostring(L,1);
		if (addrname[0] == '.' || addrname[0] == ':') {
			dest = skynet_queryname(context, addrname);
			if (dest == 0) {
				luaL_error(L, "Invalid name %s", addrname);
			}
		} else if ('0' <= addrname[0] && addrname[0] <= '9') {
			luaL_error(L, "Invalid name %s: must not start with a digit", addrname);
		} else {
			return _sendname(L, context, addrname);
		}
		break;
	}
	default:
		return luaL_error(L, "address must be number or string, got %s",lua_typename(L,addr_type));
	}

	int type = luaL_checkinteger(L, 2);
	int session = 0;
	if (lua_isnil(L,3)) {
		type |= PTYPE_TAG_ALLOCSESSION;
	} else {
		session = luaL_checkinteger(L,3);
	}

	int mtype = lua_type(L,4);
	switch (mtype) {
	case LUA_TSTRING: {
		size_t len = 0;
		void * msg = (void *)lua_tolstring(L,4,&len);
		if (len == 0) {
			msg = NULL;
		}
		session = skynet_send(context, 0, dest, type, session , msg, len);
		break;
	}
	case LUA_TLIGHTUSERDATA: {
		void * msg = lua_touserdata(L,4);
		int size = luaL_checkinteger(L,5);
		session = skynet_send(context, 0, dest, type | PTYPE_TAG_DONTCOPY, session, msg, size);
		break;
	}
	default:
		luaL_error(L, "skynet.send invalid param %s", lua_type(L,4));
	}
	lua_pushinteger(L,session);
	return 1;
}

static int
_redirect(lua_State *L) {
	struct skynet_context * context = lua_touserdata(L, lua_upvalueindex(1));
	uint32_t dest = luaL_checkunsigned(L,1);
	uint32_t source = luaL_checkunsigned(L,2);
	int type = luaL_checkinteger(L,3);
	int session = luaL_checkinteger(L,4);

	int mtype = lua_type(L,5);
	switch (mtype) {
	case LUA_TSTRING: {
		size_t len = 0;
		void * msg = (void *)lua_tolstring(L,5,&len);
		if (len == 0) {
			msg = NULL;
		}
		session = skynet_send(context, source, dest, type, session , msg, len);
		break;
	}
	case LUA_TLIGHTUSERDATA: {
		void * msg = lua_touserdata(L,5);
		int size = luaL_checkinteger(L,6);
		session = skynet_send(context, source, dest, type | PTYPE_TAG_DONTCOPY, session, msg, size);
		break;
	}
	default:
		luaL_error(L, "skynet.redirect invalid param %s", lua_type(L,5));
	}
	return 0;
}

static int
_forward(lua_State *L) {
	struct skynet_context * context = lua_touserdata(L, lua_upvalueindex(1));
	uint32_t dest = luaL_checkunsigned(L,1);
	skynet_forward(context, dest);

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

static int
_harbor(lua_State *L) {
	struct skynet_context * context = lua_touserdata(L, lua_upvalueindex(1));
	uint32_t handle = luaL_checkunsigned(L,1);
	int harbor = 0;
	int remote = skynet_isremote(context, handle, &harbor);
	lua_pushinteger(L,harbor);
	lua_pushboolean(L, remote);

	return 2;
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
		{ "forward", _forward },
		{ "command" , _command },
		{ "callback" , _callback },
		{ "error", _error },
		{ "tostring", _tostring },
		{ "harbor", _harbor },
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

	luaL_Reg l2[] = {
		{ "stat", _stat },
		{ "remote_init", remoteobj_init },
		{ NULL, NULL },
	};


	luaL_setfuncs(L,l2,0);

	return 1;
}
