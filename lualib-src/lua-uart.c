#include "skynet_malloc.h"

#include <lua.h>
#include <lauxlib.h>

#include <sys/socket.h>
#include <arpa/inet.h>
#include <string.h>

#include "skynet_socket.h"

static int
luart_open(lua_State *L) {
	struct skynet_context * ctx = lua_touserdata(L, lua_upvalueindex(1));
	const char *port = luaL_checkstring(L,1);
	int id =  skynet_uart_open(ctx, port);
	if (id < 0) {
		return luaL_error(L, "Open error");
	}
	lua_pushinteger(L, id);
	return 1;
}

static int
luart_set(lua_State *L){
	struct skynet_context * ctx = lua_touserdata(L, lua_upvalueindex(1));
	int id = luaL_checkinteger(L, 1);
	int speed = luaL_checkinteger(L, 2);
	int flow_ctrl = luaL_checkinteger(L, 3);
	int databits = luaL_checkinteger(L, 4);
	int stopbits = luaL_checkinteger(L, 5);
	const char* p = luaL_checkstring(L, 6);
	int parity = p[0];
	int ok =  skynet_uart_set(ctx, id, speed, flow_ctrl, databits, stopbits, parity);
	if (ok < 0) {
		return luaL_error(L, "Set error");
	}
	lua_pushinteger(L, ok);
	return 1;
}

static int
ldrop(lua_State *L) {
	void * msg = lua_touserdata(L, 1);
	luaL_checkinteger(L, 2);
	skynet_free(msg);
	return 0;
}

static void
concat_table(lua_State *L, int index, void *buffer, size_t tlen) {
	char *ptr = buffer;
	int i;
	for (i = 1; lua_geti(L, index, i) != LUA_TNIL; ++i) {
		size_t len;
		const char * str = lua_tolstring(L, -1, &len);
		if (str == NULL || tlen < len) {
			break;
		}
		memcpy(ptr, str, len);
		ptr += len;
		tlen -= len;
		lua_pop(L, 1);
	}
	if (tlen != 0) {
		skynet_free(buffer);
		luaL_error(L, "Invalid strings table");
	}
	lua_pop(L, 1);
}

static size_t
count_size(lua_State *L, int index) {
	size_t tlen = 0;
	int i;
	for (i = 1; lua_geti(L, index, i) != LUA_TNIL; ++i) {
		size_t len;
		luaL_checklstring(L, -1, &len);
		tlen += len;
		lua_pop(L, 1);
	}
	lua_pop(L, 1);
	return tlen;
}

static void *
get_buffer(lua_State *L, int index, int *sz) {
	void *buffer;
	switch (lua_type(L, index)) {
		const char * str;
		size_t len;
	case LUA_TUSERDATA:
	case LUA_TLIGHTUSERDATA:
		buffer = lua_touserdata(L, index);
		*sz = luaL_checkinteger(L, index + 1);
		break;
	case LUA_TTABLE:
		// concat the table as a string
		len = count_size(L, index);
		buffer = skynet_malloc(len);
		concat_table(L, index, buffer, len);
		*sz = (int)len;
		break;
	default:
		str = luaL_checklstring(L, index, &len);
		buffer = skynet_malloc(len);
		memcpy(buffer, str, len);
		*sz = (int)len;
		break;
	}
	return buffer;
}

static int
lunpack(lua_State *L) {
	struct skynet_socket_message *message = lua_touserdata(L, 1);
	int size = luaL_checkinteger(L, 2);

	lua_pushinteger(L, message->type);
	lua_pushinteger(L, message->id);
	lua_pushinteger(L, message->ud);
	if (message->buffer == NULL) {
		lua_pushlstring(L, (char *)(message + 1), size - sizeof(*message));
	}
	else {
		lua_pushlightuserdata(L, message->buffer);
	}
	return 4;
}

static int
luart_send(lua_State *L){
	struct skynet_context * ctx = lua_touserdata(L, lua_upvalueindex(1));
	int id = luaL_checkinteger(L, 1);
	int sz = 0;
	void *buffer = get_buffer(L, 2, &sz);
	skynet_uart_send(ctx, id, buffer, sz);
	return 1;
}

static int
luart_close(lua_State *L) {
	struct skynet_context * ctx = lua_touserdata(L, lua_upvalueindex(1));
	int id = luaL_checkinteger(L, 1);
	skynet_uart_close(ctx, id);
	return 0;
}

int
luaopen_uartdriver(lua_State *L) {
	luaL_checkversion(L);
	luaL_Reg l[] = {
		{ "drop", ldrop },
		{ "unpack", lunpack },
		{ NULL, NULL },
	}; 
	luaL_newlib(L, l);

	luaL_Reg l2[] = {
		{ "open", luart_open },
		{ "set", luart_set },
		{ "send", luart_send },
		{ "close", luart_close },
		{ NULL, NULL },
	};
	lua_getfield(L, LUA_REGISTRYINDEX, "skynet_context");
	struct skynet_context *ctx = lua_touserdata(L, -1);
	if (ctx == NULL) {
		return luaL_error(L, "Init skynet context first");
	}

	luaL_setfuncs(L, l2, 1);

	return 1;
}



