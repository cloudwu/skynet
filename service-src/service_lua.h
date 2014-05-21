#ifndef SKYNET_SERVICE_LUA_H
#define SKYNET_SERVICE_LUA_H

// snlua结构 用于lua服务 每一个lua服务实际上是 lua层的服务 + c层的snlua服务

struct snlua {
	lua_State * L;
	const char * reload;
	struct skynet_context * ctx;
	struct tqueue * tq;
	int (*init)(struct snlua *l, struct skynet_context *ctx, const char * args);
};

#endif
