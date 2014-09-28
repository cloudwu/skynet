#include "skynet.h"
#include "skynet_env.h"

#include <lua.h>
#include <lauxlib.h>

#include <stdlib.h>
#include <assert.h>

struct skynet_env {
	int lock;
	lua_State *L;
};

static struct skynet_env *E = NULL;

#define LOCK(q) while (__sync_lock_test_and_set(&(q)->lock,1)) {}  // 原子操作, 将 (q)->lock 设置为1, 并返回之前的值 // __sync_lock_test_and_set这里为何要用 while?
#define UNLOCK(q) __sync_lock_release(&(q)->lock); // 将 (q)->lock 设置为0

const char * 
skynet_getenv(const char *key) {
	LOCK(E)

	lua_State *L = E->L;
	
	lua_getglobal(L, key);
	const char * result = lua_tostring(L, -1); // todo -1表示栈顶的第1个位置?
	lua_pop(L, 1); // 见skynet_setenv()函数的注释

	UNLOCK(E)

	return result;
}

void 
skynet_setenv(const char *key, const char *value) {
	LOCK(E)
	
	lua_State *L = E->L;
	lua_getglobal(L, key); 
	assert(lua_isnil(L, -1));
	lua_pop(L,1); // 因为上面的 lua_getglobal从Lua环境中取得全局变量压入栈顶, 为了平衡（即调用前与调用后栈里的数据量不变），使用lua_pop弹出由lua_getglobal压入的数据
	lua_pushstring(L,value);  // 将字符串 value 压入栈顶
	lua_setglobal(L,key); // 把栈顶的数据传到 lua 环境中作为全局变量

	UNLOCK(E)
}

void
skynet_env_init() {
	E = skynet_malloc(sizeof(*E));
	E->lock = 0;
	E->L = luaL_newstate();
}
