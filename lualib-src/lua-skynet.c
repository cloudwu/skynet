#include "skynet.h"
#include "trace_service.h"
#include "lua-seri.h"
#include "service_lua.h"
#include "timingqueue.h"

#include "luacompat52.h"

#define KNRM  "\x1B[0m"
#define KRED  "\x1B[31m"

#include <lua.h>
#include <lauxlib.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <time.h>

#if defined(__APPLE__)
#include <mach/task.h>
#include <mach/mach.h>
#endif

struct stat {
	lua_State *L;
	int count;
	uint32_t ti_sec;
	uint32_t ti_nsec;
	struct trace_pool *trace;
	struct tqueue * tq;
	struct snlua *lua;
};

static void
_stat_begin(struct stat *S, struct timespec *ti) {
	S->count++;
	current_time(ti);
}

inline static void
_stat_end(struct stat *S, struct timespec *ti) {
	diff_time(ti, &S->ti_sec, &S->ti_nsec);
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
	if (strcmp(what,"trace")==0) {
		lua_pushlightuserdata(L, S->trace);
		return 1;
	}
	return 0;
}

static inline double
current_time_tick(struct stat *S) {
	return (double)S->ti_sec + (double)S->ti_nsec / NANOSEC;
}

static struct stat *
get_stat(lua_State *L) {
	struct stat * S = lua_touserdata(L, lua_upvalueindex(2));
	if (S == NULL) {
		lua_rawgetp(L, LUA_REGISTRYINDEX, _stat);
		S = lua_touserdata(L, -1);
		lua_replace(L, lua_upvalueindex(2));
	}

	return S;
}

static inline void
save_session(lua_State *L, int type, int session) {
	if (session > 0 && (type & 0xff) != PTYPE_RESPONSE) {
		struct stat * S = get_stat(L);
		tqueue_push(S->tq, session, current_time_tick(S));
	}
}

static int
_cb(struct skynet_context * context, void * ud, int type, int session, uint32_t source, const void * msg, size_t sz) {
	struct stat *S = ud;
	lua_State *L = S->L;
	struct timespec ti;
	_stat_begin(S, &ti);
	int trace = 1;
	int r;
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

	if (type == PTYPE_RESPONSE && session > 0) {
		double t = tqueue_pop(S->tq, session);
		if (t != 0) {
			t = current_time_tick(S) - t;
			lua_pushnumber(L, t);
			r = lua_pcall(L, 6, 0 , trace);
		} else {
			r = lua_pcall(L, 5, 0 , trace);
		}
	} else {
		r = lua_pcall(L, 5, 0 , trace);
	}

	_stat_end(S, &ti);

	struct trace_info *tti = trace_yield(S->trace);
	if (tti) {
		skynet_error(context, "Untraced time %f",  trace_delete(S->trace, tti));
	}

	if (r == LUA_OK) {
		if (S->lua->reload) {
			skynet_callback(context, NULL, 0);
			struct snlua * lua = S->lua;
			assert(lua->L == L);
			const char * cmd = lua->reload;
			lua->reload = NULL;
			lua->L = luaL_newstate();
			int err = lua->init(lua, context, cmd);
			if (err) {
				skynet_callback(context, S, _cb);
				skynet_error(context, "lua reload failed : %s", cmd);
				lua_close(lua->L);
				lua->L = L;
			} else {
				skynet_error(context, "lua reload %s", cmd);
				lua_close(L);
			}
		}
		return 0;
	}
	const char * self = skynet_command(context, "REG", NULL);
	switch (r) {
	case LUA_ERRRUN:
		skynet_error(context, "lua call [%x to %s : %d msgsz = %d] error : " KRED "%s" KNRM, source , self, session, sz, lua_tostring(L,-1));
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
_timing(lua_State *L) {
	int session = luaL_checkinteger(L,1);
	struct stat * S = get_stat(L);
	double t = tqueue_pop(S->tq, session);
	if (t != 0) {
		t = current_time_tick(S) - t;
	}
	lua_pushnumber(L, t);

	return 1;
}

static int
_delete_stat(lua_State *L) {
	struct stat * S = lua_touserdata(L,1);
	trace_release(S->trace);
	tqueue_delete(S->tq);
	return 0;
}

static int
_callback(lua_State *L) {
	struct snlua *lua = lua_touserdata(L, lua_upvalueindex(1));
	if (lua == NULL || lua->ctx == NULL) {
		return luaL_error(L, "Init skynet context first");
	}
	struct skynet_context * context = lua->ctx;

	luaL_checktype(L,1,LUA_TFUNCTION);
	lua_settop(L,1);
	lua_rawsetp(L, LUA_REGISTRYINDEX, _cb);

	lua_rawgeti(L, LUA_REGISTRYINDEX, LUA_RIDX_MAINTHREAD);
	lua_State *gL = lua_tothread(L,-1);

	struct stat * S = lua_newuserdata(L, sizeof(*S));
	memset(S, 0, sizeof(*S));
	S->L = gL;
	S->trace = trace_create();
	S->tq = tqueue_new();
	S->lua = lua;

	lua_createtable(L,0,1);
	lua_pushcfunction(L, _delete_stat);
	lua_setfield(L,-2,"__gc");
	lua_setmetatable(L, -2);

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
		save_session(L, type, session);
		break;
	}
	case LUA_TNIL :
		session = skynet_sendname(context, dest, type, session , NULL, 0);
		save_session(L, type, session);
		break;
	case LUA_TLIGHTUSERDATA: {
		luaL_checktype(L, 4, LUA_TLIGHTUSERDATA);
		void * msg = lua_touserdata(L,4);
		int size = luaL_checkinteger(L,5);
		session = skynet_sendname(context, dest, type | PTYPE_TAG_DONTCOPY, session, msg, size);
		save_session(L, type, session);
		break;
	}
	default:
		luaL_error(L, "skynet.send invalid param %s", lua_type(L,4));
	}
	if (session < 0) {
		luaL_error(L, "skynet.send session (%d) < 0", session);
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
		save_session(L, type, session);
		break;
	}
	case LUA_TLIGHTUSERDATA: {
		void * msg = lua_touserdata(L,4);
		int size = luaL_checkinteger(L,5);
		session = skynet_send(context, 0, dest, type | PTYPE_TAG_DONTCOPY, session, msg, size);
		save_session(L, type, session);
		break;
	}
	default:
		luaL_error(L, "skynet.send invalid param %s", lua_type(L,4));
	}
	if (session < 0) {
		// send to invalid address
		// todo: maybe throw error is better
		return 0;
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
		save_session(L, type, session);
		break;
	}
	case LUA_TLIGHTUSERDATA: {
		void * msg = lua_touserdata(L,5);
		int size = luaL_checkinteger(L,6);
		session = skynet_send(context, source, dest, type | PTYPE_TAG_DONTCOPY, session, msg, size);
		save_session(L, type, session);
		break;
	}
	default:
		luaL_error(L, "skynet.redirect invalid param %s", lua_typename(L,mtype));
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

static int
_context(lua_State *L) {
	struct skynet_context * context = lua_touserdata(L, lua_upvalueindex(1));
	lua_pushlightuserdata(L, context);

	return 1;
}

// trace api
static int
_trace_new(lua_State *L) {
	struct trace_pool *p = lua_touserdata(L,1);
	struct trace_info *t = trace_new(p);
	if (t==NULL) {
		return luaL_error(L, "Last trace didn't close");
	}
	lua_pushlightuserdata(L,t);
	return 1;
}

static int
_trace_delete(lua_State *L) {
	struct trace_pool *p = lua_touserdata(L,1);
	struct trace_info *t = lua_touserdata(L,2);
	double ti = trace_delete(p,t);
	lua_pushnumber(L, ti);
	return 1;
}

static int
_trace_switch(lua_State *L) {
	int session = luaL_checkinteger(L,2);
	if (session <=0)
		return 0;
	struct trace_pool *p = lua_touserdata(L,1);
	trace_switch(p, session);
	return 0;
}

static int
_trace_yield(lua_State *L) {
	struct trace_pool *p = lua_touserdata(L,1);
	struct trace_info * t = trace_yield(p);
	if (t) {
		lua_pushlightuserdata(L,t);
		return 1;
	}
	return 0;
}

static int
_trace_register(lua_State *L) {
	int session = luaL_checkinteger(L,2);
	if (session <=0)
		return 0;
	struct trace_pool *p = lua_touserdata(L,1);
	trace_register(p, session);
	return 0;
}

static int
_reload(lua_State *L) {
	struct snlua *lua = lua_touserdata(L,lua_upvalueindex(1));
	lua->reload = luaL_checkstring(L,1);
	lua_settop(L,1);
	lua_replace(L,lua_upvalueindex(2));
	return 0;
}

int
luaopen_skynet_c(lua_State *L) {
	luaL_checkversion(L);
	
	luaL_Reg l[] = {
		{ "send" , _send },
		{ "genid", _genid },
		{ "timing", _timing },
		{ "redirect", _redirect },
		{ "forward", _forward },
		{ "command" , _command },
		{ "error", _error },
		{ "tostring", _tostring },
		{ "harbor", _harbor },
		{ "context", _context },
		{ "pack", _luaseri_pack },
		{ "unpack", _luaseri_unpack },
		{ NULL, NULL },
	};

	luaL_Reg l2[] = {
		{ "stat", _stat },
		{ "trace_new", _trace_new },
		{ "trace_delete", _trace_delete },
		{ "trace_switch", _trace_switch },
		{ "trace_yield", _trace_yield },
		{ "trace_register", _trace_register },
		{ NULL, NULL },
	};

	lua_createtable(L, 0, (sizeof(l) + sizeof(l2))/sizeof(luaL_Reg)-1);

	lua_getfield(L, LUA_REGISTRYINDEX, "skynet_lua");
	struct snlua *lua = lua_touserdata(L,-1);
	if (lua == NULL || lua->ctx == NULL) {
		return luaL_error(L, "Init skynet context first");
	}
	assert(lua->L == L);

	lua_pushvalue(L,-1);
	lua_pushcclosure(L,_callback,1);
	lua_setfield(L, -3, "callback");

	lua_pushnil(L);
	lua_pushcclosure(L,_reload,2);
	lua_setfield(L, -2, "reload");

	lua_pushlightuserdata(L, lua->ctx);
	lua_pushnil(L);
	luaL_setfuncs(L,l,2);

	luaL_setfuncs(L,l2,0);

	return 1;
}
