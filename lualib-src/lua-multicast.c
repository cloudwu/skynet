#define LUA_LIB

#include "skynet.h"

#include <lua.h>
#include <lauxlib.h>
#include <stdint.h>
#include <string.h>

#include "atomic.h"

struct mc_package {
	int reference;
	uint32_t size;
	void *data;
};

static int
pack(lua_State *L, void *data, size_t size) {
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
	lightuserdata
	integer size

	return lightuserdata, sizeof(struct mc_package *)
 */
static int
mc_packlocal(lua_State *L) {
	void * data = lua_touserdata(L, 1);
	size_t size = (size_t)luaL_checkinteger(L, 2);
	if (size != (uint32_t)size) {
		return luaL_error(L, "Size should be 32bit integer");
	}
	return pack(L, data, size);
}

/*
	lightuserdata
	integer size

	return lightuserdata, sizeof(struct mc_package *)
 */
static int
mc_packremote(lua_State *L) {
	void * data = lua_touserdata(L, 1);
	size_t size = (size_t)luaL_checkinteger(L, 2);
	if (size != (uint32_t)size) {
		return luaL_error(L, "Size should be 32bit integer");
	}
	void * msg = skynet_malloc(size);
	memcpy(msg, data, size);
	return pack(L, msg, size);
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
	if (sz != sizeof(pack)) {
		return luaL_error(L, "Invalid multicast package size %d", sz);
	}
	lua_pushlightuserdata(L, *pack);
	lua_pushlightuserdata(L, (*pack)->data);
	lua_pushinteger(L, (lua_Integer)((*pack)->size));
	return 3;
}

/*
	lightuserdata struct mc_package **
	integer reference

	return mc_package *
 */
static int
mc_bindrefer(lua_State *L) {
	struct mc_package ** pack = lua_touserdata(L,1);
	int ref = luaL_checkinteger(L,2);
	if ((*pack)->reference != 0) {
		return luaL_error(L, "Can't bind a multicast package more than once");
	}
	(*pack)->reference = ref;

	lua_pushlightuserdata(L, *pack);

	skynet_free(pack);

	return 1;
}

/*
	lightuserdata struct mc_package *
 */
static int
mc_closelocal(lua_State *L) {
	struct mc_package *pack = lua_touserdata(L,1);

	int ref = ATOM_DEC(&pack->reference);
	if (ref <= 0) {
		skynet_free(pack->data);
		skynet_free(pack);
		if (ref < 0) {
			return luaL_error(L, "Invalid multicast package reference %d", ref);
		}
	}

	return 0;
}

/*
	lightuserdata struct mc_package **
	return lightuserdata/size
 */
static int
mc_remote(lua_State *L) {
	struct mc_package **ptr = lua_touserdata(L,1);
	struct mc_package *pack = *ptr;
	lua_pushlightuserdata(L, pack->data);
	lua_pushinteger(L, (lua_Integer)(pack->size));
	skynet_free(pack);
	skynet_free(ptr);
	return 2;
}

static int
mc_nextid(lua_State *L) {
	uint32_t id = (uint32_t)luaL_checkinteger(L, 1);
	id += 256;
	// remove the highest bit, see #1139
	lua_pushinteger(L, id & 0x7fffffffu);

	return 1;
}

LUAMOD_API int
luaopen_skynet_multicast_core(lua_State *L) {
	luaL_Reg l[] = {
		{ "pack", mc_packlocal },
		{ "unpack", mc_unpacklocal },
		{ "bind", mc_bindrefer },
		{ "close", mc_closelocal },
		{ "remote", mc_remote },
		{ "packremote", mc_packremote },
		{ "nextid", mc_nextid },
		{ NULL, NULL },
	};
	luaL_checkversion(L);
	luaL_newlib(L,l);
	return 1;
}
