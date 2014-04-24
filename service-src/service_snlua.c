#include "skynet.h"

#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>
#include <assert.h>

#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

struct snlua {
	lua_State * L;
	struct skynet_context * ctx;
	const char * preload;
};

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
		{ NULL, NULL },
	};
	luaL_newlib(L,l);
  lua_getglobal(L, "loadfile");
  lua_setfield(L, -2, "loadfile");
	return 1;
}

#endif

static int
_try_load(lua_State *L, const char * path, int pathlen, const char * name) {
	int namelen = strlen(name);
	char tmp[pathlen + namelen];
	int i;
	for (i=0;i<pathlen;i++) {
		if (path[i] == '?')
			break;
		tmp[i] = path[i];
	}
	if (path[i] == '?') {
		memcpy(tmp+i,name,namelen);
		memcpy(tmp+i+namelen,path+i+1,pathlen - i -1);
	} else {
		fprintf(stderr,"snlua : Invalid lua service path\n");
		exit(1);
	}
	tmp[namelen+pathlen-1] = '\0';
	int r = luaL_loadfile(L,tmp);
	if (r == LUA_OK) {
		int i;
		for (i=namelen+pathlen-2;i>=0;i--) {
			if (tmp[i] == '/') {
				lua_pushlstring(L,tmp,i+1);
				lua_setglobal(L,"SERVICE_PATH");
				break;
			}
		}
		if (i<0) {
			return 0;
		}
		lua_getglobal(L,"package");
		lua_getfield(L,-1,"path");
		luaL_Buffer b;
		luaL_buffinit(L, &b);
		luaL_addlstring(&b, tmp, i+1);
		luaL_addstring(&b, "?.lua;");
		luaL_addvalue(&b);
		luaL_pushresult(&b);
		lua_setfield(L,-2,"path");
		lua_pop(L,1);
		return 0;
	} else if (r == LUA_ERRFILE) {
		lua_pop(L,1);
		return -1;
	}
	return 1;
}

static int
_load(lua_State *L, char ** filename) {
	const char * name = strsep(filename, " \r\n");
	const char * path = skynet_command(NULL, "GETENV", "luaservice");
	while (path[0]) {
		int pathlen;
		char * pathend = strchr(path,';');
		if (pathend) {
			pathlen = pathend - path;
		} else {
			pathlen = strlen(path);
		}
		int r = _try_load(L, path, pathlen, name);
		if (r >=0) {
			return r;
		}
		path+=pathlen;
		if (path[0]==';')
			++path;
	}
	return -1;
}

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

static void
_report_error(lua_State *L, struct skynet_context *ctx, const char *filename, int err) {
	switch (err) {
	case LUA_ERRRUN:
		skynet_error(ctx, "lua do [%s] error : %s", filename, lua_tostring(L,-1));
		break;
	case LUA_ERRMEM:
		skynet_error(ctx, "lua memory error : %s",filename);
		break;
	case LUA_ERRERR:
		skynet_error(ctx, "lua message error : %s",filename);
		break;
	case LUA_ERRGCMM:
		skynet_error(ctx, "lua gc error : %s",filename);
		break;
	};
	lua_pop(L,1);
}

static void
_report_launcher_error(struct skynet_context *ctx) {
	// sizeof "ERROR" == 5
	skynet_sendname(ctx, ".launcher", PTYPE_TEXT, 0, "ERROR", 5);
}

static int
dofile(lua_State *L, struct skynet_context *ctx, const char * args) {
	int traceback_index = 1;
	char tmp[strlen(args)+1];
	char *parm = tmp;
	strcpy(parm,args);

	const char * filename = parm;
	int r = _load(L, &parm);
	if (r != 0) {
		if (r<0) {
			skynet_error(ctx, "lua parser [%s] load error", filename);
		} else {
			skynet_error(ctx, "lua parser [%s] error : %s", filename, lua_tostring(L,-1));
		}
		_report_launcher_error(ctx);
		return 1;
	}
	int n=0;
	while(parm) {
		const char * arg = strsep(&parm, " \r\n");
		if (arg && arg[0]!='\0') {
			lua_pushstring(L, arg);
			++n;
		}
	}
	r = lua_pcall(L,n,0,traceback_index);
	if (r == LUA_OK) {
		r = lua_gc(L, LUA_GCCOLLECT, 0);
		if (r == LUA_OK) {
			return 0;
		}
	}
	_report_error(L, ctx, filename, r);
	_report_launcher_error(ctx);
	return 1;
}

static int
_init(struct snlua *l, struct skynet_context *ctx, const char * args) {
	lua_State *L = l->L;
	l->ctx = ctx;
	lua_gc(L, LUA_GCSTOP, 0);
	luaL_openlibs(L);
	lua_pushlightuserdata(L, ctx);
	lua_setfield(L, LUA_REGISTRYINDEX, "skynet_context");
	luaL_requiref(L, "skynet.codecache", codecache , 0);
	lua_pop(L,1);

	lua_pushcfunction(L, traceback);
	assert(lua_gettop(L) == 1);

	if (l->preload) {
		size_t l1 = strlen(l->preload);
		size_t l2 = strlen(args);
		char tmp[l1 + l2 + 2];
		sprintf(tmp, "%s %s", l->preload, args);
		if (dofile(L, ctx, tmp)) {
			return 1;
		}
	}
	if (dofile(L, ctx, args)) {
		return 1;
	}

	lua_gc(L, LUA_GCRESTART, 0);

	return 0;
}

static int
_launch(struct skynet_context * context, void *ud, int type, int session, uint32_t source , const void * msg, size_t sz) {
	assert(type == 0 && session == 0);
	struct snlua *l = ud;
	skynet_callback(context, NULL, NULL);
	int err = _init(l, context, msg);
	if (err) {
		skynet_command(context, "EXIT", NULL);
	}

	return 0;
}

int
snlua_init(struct snlua *l, struct skynet_context *ctx, const char * args) {
	const char * preload = skynet_command(ctx, "GETENV", "preload");
	if (preload && preload[0]) {
		l->preload = skynet_strdup(preload);
	}
	int sz = strlen(args);
	char * tmp = skynet_malloc(sz+1);
	memcpy(tmp, args, sz+1);
	skynet_callback(ctx, l , _launch);
	const char * self = skynet_command(ctx, "REG", NULL);
	uint32_t handle_id = strtoul(self+1, NULL, 16);
	// it must be first message
	skynet_send(ctx, 0, handle_id, PTYPE_TAG_DONTCOPY,0, tmp, sz+1);
	return 0;
}

struct snlua *
snlua_create(void) {
	struct snlua * l = skynet_malloc(sizeof(*l));
	memset(l,0,sizeof(*l));
	l->L = lua_newstate(skynet_lalloc, NULL);
	l->preload = NULL;
	return l;
}

void
snlua_release(struct snlua *l) {
	lua_close(l->L);
	skynet_free((void*)l->preload);
	skynet_free(l);
}
