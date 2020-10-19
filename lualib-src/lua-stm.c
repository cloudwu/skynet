#define LUA_LIB

#include <lua.h>
#include <lauxlib.h>
#include <stdlib.h>
#include <stdint.h>
#include <assert.h>
#include <string.h>

#include "rwlock.h"
#include "skynet_malloc.h"
#include "atomic.h"

struct stm_object {
	struct rwlock lock;
	int reference;
	struct stm_copy * copy;
};

struct stm_copy {
	int reference;
	uint32_t sz;
	void * msg;
};

// msg should alloc by skynet_malloc 
static struct stm_copy *
stm_newcopy(void * msg, int32_t sz) {
	struct stm_copy * copy = skynet_malloc(sizeof(*copy));
	copy->reference = 1;
	copy->sz = sz;
	copy->msg = msg;

	return copy;
}

static struct stm_object *
stm_new(void * msg, int32_t sz) {
	struct stm_object * obj = skynet_malloc(sizeof(*obj));
	rwlock_init(&obj->lock);
	obj->reference = 1;
	obj->copy = stm_newcopy(msg, sz);

	return obj;
}

static void
stm_releasecopy(struct stm_copy *copy) {
	if (copy == NULL)
		return;
	if (ATOM_DEC(&copy->reference) == 0) {
		skynet_free(copy->msg);
		skynet_free(copy);
	}
}

static void
stm_release(struct stm_object *obj) {
	assert(obj->copy);
	rwlock_wlock(&obj->lock);
	// writer release the stm object, so release the last copy .
	stm_releasecopy(obj->copy);
	obj->copy = NULL;
	if (--obj->reference > 0) {
		// stm object grab by readers, reset the copy to NULL.
		rwlock_wunlock(&obj->lock);
		return;
	}
	// no one grab the stm object, no need to unlock wlock.
	skynet_free(obj);
}

static void
stm_releasereader(struct stm_object *obj) {
	rwlock_rlock(&obj->lock);
	if (ATOM_DEC(&obj->reference) == 0) {
		// last reader, no writer. so no need to unlock
		assert(obj->copy == NULL);
		skynet_free(obj);
		return;
	}
	rwlock_runlock(&obj->lock);
}

static void
stm_grab(struct stm_object *obj) {
	rwlock_rlock(&obj->lock);
	int ref = ATOM_FINC(&obj->reference);
	rwlock_runlock(&obj->lock);
	assert(ref > 0);
}

static struct stm_copy *
stm_copy(struct stm_object *obj) {
	rwlock_rlock(&obj->lock);
	struct stm_copy * ret = obj->copy;
	if (ret) {
		int ref = ATOM_FINC(&ret->reference);
		assert(ref > 0);
	}
	rwlock_runlock(&obj->lock);
	
	return ret;
}

static void
stm_update(struct stm_object *obj, void *msg, int32_t sz) {
	struct stm_copy *copy = stm_newcopy(msg, sz);
	rwlock_wlock(&obj->lock);
	struct stm_copy *oldcopy = obj->copy;
	obj->copy = copy;
	rwlock_wunlock(&obj->lock);

	stm_releasecopy(oldcopy);
}

// lua binding

struct boxstm {
	struct stm_object * obj;
};

static int
lcopy(lua_State *L) {
	struct boxstm * box = lua_touserdata(L, 1);
	stm_grab(box->obj);
	lua_pushlightuserdata(L, box->obj);
	return 1;
}

static int
lnewwriter(lua_State *L) {
	void * msg;
	size_t sz;
	if (lua_isuserdata(L,1)) {
		msg = lua_touserdata(L, 1);
		sz = (size_t)luaL_checkinteger(L, 2);
	} else {
		const char * tmp = luaL_checklstring(L,1,&sz);
		msg = skynet_malloc(sz);
		memcpy(msg, tmp, sz);
	}
	struct boxstm * box = lua_newuserdatauv(L, sizeof(*box), 0);
	box->obj = stm_new(msg,sz);
	lua_pushvalue(L, lua_upvalueindex(1));
	lua_setmetatable(L, -2);

	return 1;
}

static int
ldeletewriter(lua_State *L) {
	struct boxstm * box = lua_touserdata(L, 1);
	stm_release(box->obj);
	box->obj = NULL;

	return 0;
}

static int
lupdate(lua_State *L) {
	struct boxstm * box = lua_touserdata(L, 1);
	void * msg;
	size_t sz;
	if (lua_isuserdata(L, 2)) {
		msg = lua_touserdata(L, 2);
		sz = (size_t)luaL_checkinteger(L, 3);
	} else {
		const char * tmp = luaL_checklstring(L,2,&sz);
		msg = skynet_malloc(sz);
		memcpy(msg, tmp, sz);
	}
	stm_update(box->obj, msg, sz);

	return 0;
}

struct boxreader {
	struct stm_object *obj;
	struct stm_copy *lastcopy;
};

static int
lnewreader(lua_State *L) {
	struct boxreader * box = lua_newuserdatauv(L, sizeof(*box), 0);
	box->obj = lua_touserdata(L, 1);
	box->lastcopy = NULL;
	lua_pushvalue(L, lua_upvalueindex(1));
	lua_setmetatable(L, -2);

	return 1;
}

static int
ldeletereader(lua_State *L) {
	struct boxreader * box = lua_touserdata(L, 1);
	stm_releasereader(box->obj);
	box->obj = NULL;
	stm_releasecopy(box->lastcopy);
	box->lastcopy = NULL;

	return 0;
}

static int
lread(lua_State *L) {
	struct boxreader * box = lua_touserdata(L, 1);
	luaL_checktype(L, 2, LUA_TFUNCTION);

	struct stm_copy * copy = stm_copy(box->obj);
	if (copy == box->lastcopy) {
		// not update
		stm_releasecopy(copy);
		lua_pushboolean(L, 0);
		return 1;
	}

	stm_releasecopy(box->lastcopy);
	box->lastcopy = copy;
	if (copy) {
		lua_settop(L, 3);
		lua_replace(L, 1);
		lua_settop(L, 2);
		lua_pushlightuserdata(L, copy->msg);
		lua_pushinteger(L, copy->sz);
		lua_pushvalue(L, 1);
		lua_call(L, 3, LUA_MULTRET);
		lua_pushboolean(L, 1);
		lua_replace(L, 1);
		return lua_gettop(L);
	} else {
		lua_pushboolean(L, 0);
		return 1;
	}
}

LUAMOD_API int
luaopen_skynet_stm(lua_State *L) {
	luaL_checkversion(L);
	lua_createtable(L, 0, 3);

	lua_pushcfunction(L, lcopy);
	lua_setfield(L, -2, "copy");

	luaL_Reg writer[] = {
		{ "new", lnewwriter },
		{ NULL, NULL },
	};
	lua_createtable(L, 0, 2);
	lua_pushcfunction(L, ldeletewriter),
	lua_setfield(L, -2, "__gc");
	lua_pushcfunction(L, lupdate),
	lua_setfield(L, -2, "__call");
	luaL_setfuncs(L, writer, 1);

	luaL_Reg reader[] = {
		{ "newcopy", lnewreader },
		{ NULL, NULL },
	};
	lua_createtable(L, 0, 2);
	lua_pushcfunction(L, ldeletereader),
	lua_setfield(L, -2, "__gc");
	lua_pushcfunction(L, lread),
	lua_setfield(L, -2, "__call");
	luaL_setfuncs(L, reader, 1);

	return 1;
}
