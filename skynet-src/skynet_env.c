#include "skynet_env.h"

#include <lua.h>
#include <lauxlib.h>

#include <stdlib.h>
#include <assert.h>

struct skynet_env {
	int lock;
	lua_State *L;
};

// skynet 环境配置 主要是获取和设置lua的环境变量
static struct skynet_env *E = NULL;

#define LOCK(q) while (__sync_lock_test_and_set(&(q)->lock,1)) {} // 自旋锁的实现
#define UNLOCK(q) __sync_lock_release(&(q)->lock);

const char * 
skynet_getenv(const char *key) { // 获取的是 lua 的全局变量 key 值
	LOCK(E)

	lua_State *L = E->L;
	
	lua_getglobal(L, key);						// 获取lua全局变量key的值,并压入lua栈
	const char * result = lua_tostring(L, -1);	// 从lua栈中弹出该变量值并赋值给result
	lua_pop(L, 1);								// 弹出该变量值

	UNLOCK(E)

	return result;
}

void 
skynet_setenv(const char *key, const char *value) {
	LOCK(E)
	
	lua_State *L = E->L;
	lua_getglobal(L, key);		// 获取lua全局变量key的值,并压入lua栈
	assert(lua_isnil(L, -1));	// 断言该变量值一定是空的

	lua_pop(L,1);				// 弹出该变量值

	lua_pushstring(L,value);	// 将value压入lua栈
	lua_setglobal(L,key);		// 从lua栈中弹出value,将lua变量值设为value

	UNLOCK(E)
}

void
skynet_env_init() {
	E = malloc(sizeof(*E));
	E->lock = 0;
	E->L = luaL_newstate();
}
