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

#define LOCK(q) while (__sync_lock_test_and_set(&(q)->lock,1)) {}  // ԭ�Ӳ���, �� (q)->lock ����Ϊ1, ������֮ǰ��ֵ // __sync_lock_test_and_set����Ϊ��Ҫ�� while?
#define UNLOCK(q) __sync_lock_release(&(q)->lock); // �� (q)->lock ����Ϊ0

const char * 
skynet_getenv(const char *key) {
	LOCK(E)

	lua_State *L = E->L;
	
	lua_getglobal(L, key);
	const char * result = lua_tostring(L, -1); // todo -1��ʾջ���ĵ�1��λ��?
	lua_pop(L, 1); // ��skynet_setenv()������ע��

	UNLOCK(E)

	return result;
}

void 
skynet_setenv(const char *key, const char *value) {
	LOCK(E)
	
	lua_State *L = E->L;
	lua_getglobal(L, key); 
	assert(lua_isnil(L, -1));
	lua_pop(L,1); // ��Ϊ����� lua_getglobal��Lua������ȡ��ȫ�ֱ���ѹ��ջ��, Ϊ��ƽ�⣨������ǰ����ú�ջ������������䣩��ʹ��lua_pop������lua_getglobalѹ�������
	lua_pushstring(L,value);  // ���ַ��� value ѹ��ջ��
	lua_setglobal(L,key); // ��ջ�������ݴ��� lua ��������Ϊȫ�ֱ���

	UNLOCK(E)
}

void
skynet_env_init() {
	E = skynet_malloc(sizeof(*E));
	E->lock = 0;
	E->L = luaL_newstate();
}
