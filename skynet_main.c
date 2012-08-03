#include "skynet_imp.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>

static int
optint(lua_State *L, const char *key, int opt) {
	lua_getglobal(L, key);
	int n = lua_tointeger(L,-1);
	lua_pop(L,1);
	if (n==0)
		return opt;
	return n;
}

static const char *
optstring(lua_State *L, const char *key,const char * opt) {
	lua_getglobal(L, key);
	const char * str = lua_tostring(L,-1);
	lua_pop(L,1);
	if (str == NULL) {
		return opt;
	}
	return strdup(str);
}

int
main(int argc, char *argv[]) {
	const char * config_file = "config";
	if (argc > 1) {
		config_file = argv[1];
	}
	struct skynet_config config;

	struct lua_State *L = luaL_newstate();
	luaL_openlibs(L);	// link lua lib
	lua_close(L);

	L = luaL_newstate();

	int err = luaL_dofile(L, config_file);
	if (err) {
		fprintf(stderr,"%s\n",lua_tostring(L,-1));
		lua_close(L);
		return 1;
	} 
	const char *path = optstring(L,"lua_path","./?.lua");
	setenv("LUA_PATH",path,1);
	const char *cpath = optstring(L,"lua_cpath","./?.so");
	setenv("LUA_CPATH",cpath,1);

	config.thread =  optint(L,"thread",8);
	config.mqueue_size = optint(L,"mqueue",256);
	config.module_path = optstring(L,"cpath","./");
	config.logger = optstring(L,"logger",NULL);
	config.harbor = optint(L, "harbor", 1);
	config.master = optstring(L,"master","tcp://127.0.0.1:2012");
	config.start = optstring(L,"start","main.lua");
	config.local = optstring(L,"address","tcp://127.0.0.1:2525");
	lua_close(L);

	skynet_start(&config);

	return 0;
}
