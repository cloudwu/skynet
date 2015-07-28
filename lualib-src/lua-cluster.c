#include <lua.h>
#include <lauxlib.h>
#include <string.h>

#include "skynet.h"

/*
	uint32_t/string addr 
	uint32_t/session session
	lightuserdata msg
	uint32_t sz

	return 
		string request
		uint32_t next_session
 */

#define TEMP_LENGTH 0x10007

static void
fill_uint32(uint8_t * buf, uint32_t n) {
	buf[0] = n & 0xff;
	buf[1] = (n >> 8) & 0xff;
	buf[2] = (n >> 16) & 0xff;
	buf[3] = (n >> 24) & 0xff;
}

static void
fill_header(lua_State *L, uint8_t *buf, int sz, void *msg) {
	if (sz >= 0x10000) {
		skynet_free(msg);
		luaL_error(L, "request message is too long %d", sz);
	}
	buf[0] = (sz >> 8) & 0xff;
	buf[1] = sz & 0xff;
}

static void
packreq_number(lua_State *L, int session, void * msg, size_t sz) {
	uint32_t addr = (uint32_t)lua_tointeger(L,1);
	uint8_t buf[TEMP_LENGTH];
	fill_header(L, buf, sz+9, msg);
	buf[2] = 0;
	fill_uint32(buf+3, addr);
	fill_uint32(buf+7, (uint32_t)session);
	memcpy(buf+11,msg,sz);

	lua_pushlstring(L, (const char *)buf, sz+11);
}

static void
packreq_string(lua_State *L, int session, void * msg, size_t sz) {
	size_t namelen = 0;
	const char *name = lua_tolstring(L, 1, &namelen);
	if (name == NULL || namelen < 1 || namelen > 255) {
		skynet_free(msg);
		luaL_error(L, "name is too long %s", name);
	}

	uint8_t buf[TEMP_LENGTH];
	fill_header(L, buf, sz+5+namelen, msg);
	buf[2] = (uint8_t)namelen;
	memcpy(buf+3, name, namelen);
	fill_uint32(buf+3+namelen, (uint32_t)session);
	memcpy(buf+7+namelen,msg,sz);

	lua_pushlstring(L, (const char *)buf, sz+7+namelen);
}

static int
lpackrequest(lua_State *L) {
	void *msg = lua_touserdata(L,3);
	if (msg == NULL) {
		return luaL_error(L, "Invalid request message");
	}
	size_t sz = (size_t)luaL_checkinteger(L,4);
	int session = luaL_checkinteger(L,2);
	if (session <= 0) {
		skynet_free(msg);
		return luaL_error(L, "Invalid request session %d", session);
	}
	int addr_type = lua_type(L,1);
	if (addr_type == LUA_TNUMBER) {
		packreq_number(L, session, msg, sz);
	} else {
		packreq_string(L, session, msg, sz);
	}
	if (++session < 0) {
		session = 1;
	}
	skynet_free(msg);
	lua_pushinteger(L, session);
	return 2;
}

/*
	string packed message
	return 	
		uint32_t or string addr
		int session
		string msg
 */

static inline uint32_t
unpack_uint32(const uint8_t * buf) {
	return buf[0] | buf[1]<<8 | buf[2]<<16 | buf[3]<<24;
}

static int
unpackreq_number(lua_State *L, const uint8_t * buf, size_t sz) {
	if (sz < 9) {
		return luaL_error(L, "Invalid cluster message");
	}
	uint32_t address = unpack_uint32(buf+1);
	uint32_t session = unpack_uint32(buf+5);
	lua_pushinteger(L, (uint32_t)address);
	lua_pushinteger(L, (uint32_t)session);
	lua_pushlstring(L, (const char *)buf+9, sz-9);

	return 3;
}

static int
unpackreq_string(lua_State *L, const uint8_t * buf, size_t sz) {
	size_t namesz = buf[0];
	if (sz < namesz + 5) {
		return luaL_error(L, "Invalid cluster message");
	}
	lua_pushlstring(L, (const char *)buf+1, namesz);
	uint32_t session = unpack_uint32(buf + namesz + 1);
	lua_pushinteger(L, (uint32_t)session);
	lua_pushlstring(L, (const char *)buf+1+namesz+4, sz - namesz - 5);

	return 3;
}

static int
lunpackrequest(lua_State *L) {
	size_t sz;
	const char *msg = luaL_checklstring(L,1,&sz);
	if (msg[0] == 0) {
		return unpackreq_number(L, (const uint8_t *)msg, sz);
	} else {
		return unpackreq_string(L, (const uint8_t *)msg, sz);
	}
}

/*
	int session
	boolean ok
	lightuserdata msg
	int sz
	return string response
 */
static int
lpackresponse(lua_State *L) {
	uint32_t session = (uint32_t)luaL_checkinteger(L,1);
	// clusterd.lua:command.socket call lpackresponse,
	// and the msg/sz is return by skynet.rawcall , so don't free(msg)
	int ok = lua_toboolean(L,2);
	void * msg;
	size_t sz;
	
	if (lua_type(L,3) == LUA_TSTRING) {
		msg = (void *)lua_tolstring(L, 3, &sz);
		if (sz > 0x1000) {
			sz = 0x1000;
		}
	} else {
		msg = lua_touserdata(L,3);
		sz = (size_t)luaL_checkinteger(L, 4);
	}

	uint8_t buf[TEMP_LENGTH];
	fill_header(L, buf, sz+5, msg);
	fill_uint32(buf+2, session);
	buf[6] = ok;
	memcpy(buf+7,msg,sz);

	lua_pushlstring(L, (const char *)buf, sz+7);

	return 1;
}

/*
	string packed response
	return integer session
		boolean ok
		string msg
 */
static int
lunpackresponse(lua_State *L) {
	size_t sz;
	const char * buf = luaL_checklstring(L, 1, &sz);
	if (sz < 5) {
		return 0;
	}
	uint32_t session = unpack_uint32((const uint8_t *)buf);
	lua_pushinteger(L, (lua_Integer)session);
	lua_pushboolean(L, buf[4]);
	lua_pushlstring(L, buf+5, sz-5);

	return 3;
}

int
luaopen_cluster_core(lua_State *L) {
	luaL_Reg l[] = {
		{ "packrequest", lpackrequest },
		{ "unpackrequest", lunpackrequest },
		{ "packresponse", lpackresponse },
		{ "unpackresponse", lunpackresponse },
		{ NULL, NULL },
	};
	luaL_checkversion(L);
	luaL_newlib(L,l);

	return 1;
}
