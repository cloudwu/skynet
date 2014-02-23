#ifndef SKYNET_SERVICE_LUA_H
#define SKYNET_SERVICE_LUA_H

struct snlua {
	lua_State * L;
	const char * reload;
	struct skynet_context * ctx;
	struct tqueue * tq;
	int (*init)(struct snlua *l, struct skynet_context *ctx, const char * args);
};

#endif
