#include "skynet.h"

#include <lua.h>
#include <lauxlib.h>
#include <stdint.h>

struct mc_package {
	int reference;
	uint32_t size;
	void *data;
};

/*
	lightuserdata
	integer size

	return lightuserdata, sizeof(struct mc_package *)
 */
static int
mc_packlocal(lua_State *L) {
	void * data = lua_touserdata(L, 1);
	size_t size = luaL_checkunsigned(L, 2);
	if (size != (uint32_t)size) {
		return luaL_error(L, "Size should be 32bit integer");
	}
	struct mc_package * pack = skynet_malloc(sizeof(struct mc_package));
	pack->reference = 0;
	pack->size = (uint32_t)size;
	pack->data = data;
	struct mc_package ** ret = skynet_malloc(sizeof(*ret));
	*ret = pack;
	lua_pushlightuserdata(L, ret);
	lua_pushinteger(L, sizeof(ret));
	return 2;
}

/*
	lightuserdata struct mc_package **
	integer size (must be sizeof(struct mc_package *)

	return package, lightuserdata, size
 */
static int
mc_unpacklocal(lua_State *L) {
	struct mc_package ** pack = lua_touserdata(L,1);
	int sz = luaL_checkinteger(L,2);
	if (sz != sizeof(*pack)) {
		return luaL_error(L, "Invalid multicast package size %d", sz);
	}
	lua_settop(L, 1);
	lua_pushlightuserdata(L, (*pack)->data);
	lua_pushunsigned(L, (*pack)->size);
	return 3;
}

/*
	lightuserdata struct mc_package **
	integer reference
 */
static int
mc_bindrefer(lua_State *L) {
	struct mc_package ** pack = lua_touserdata(L,1);
	int ref = luaL_checkinteger(L,2);
	if ((*pack)->reference != 0) {
		return luaL_error(L, "Can't bind a multicast package more than once");
	}
	(*pack)->reference = ref;

	return 0;
}

/*
	lightuserdata struct mc_package **
 */
static int
mc_closelocal(lua_State *L) {
	struct mc_package **ptr = lua_touserdata(L,1);
	struct mc_package *pack = *ptr;

	int ref = __sync_sub_and_fetch(&pack->reference, 1);
	if (ref <= 0) {
		skynet_free(pack->data);
		skynet_free(pack);
		if (ref < 0) {
			return luaL_error(L, "Invalid multicast package reference %d", ref);
		}
	}

	return 0;
}

int
luaopen_multicast_c(lua_State *L) {
	luaL_Reg l[] = {
		{ "pack", mc_packlocal },
		{ "unpack", mc_unpacklocal },
		{ "bind", mc_bindrefer },
		{ "close", mc_closelocal },
		{ NULL, NULL },
	};
	luaL_checkversion(L);
	luaL_newlib(L,l);
	return 1;
}
