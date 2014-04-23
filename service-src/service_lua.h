#ifndef SKYNET_SERVICE_LUA_H
#define SKYNET_SERVICE_LUA_H

struct snlua {
	lua_State * L;
	struct skynet_context * ctx;
	const char * preload;
};

#endif
