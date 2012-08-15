#include "skynet.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <arpa/inet.h>
#include <errno.h>
#include <unistd.h>

#include <lua.h>
#include <lauxlib.h>

static int
_open(lua_State *L) {
	const char * ip = luaL_checkstring(L,1);
	int port = luaL_checkinteger(L,2);

	struct sockaddr_in my_addr;
	memset(&my_addr, 0, sizeof(struct sockaddr_in));
	my_addr.sin_family = AF_INET;
	my_addr.sin_port = htons(port);
	my_addr.sin_addr.s_addr=inet_addr(ip);

	int fd = socket(AF_INET,SOCK_STREAM,0);
	int r = connect(fd,(struct sockaddr *)&my_addr,sizeof(struct sockaddr_in));

	if (r == -1) {
		return 0;
	}

	lua_pushinteger(L,fd);
	return 1;
}

static int
_close(lua_State *L) {
	int fd = luaL_checkinteger(L,1);
	close(fd);
	return 0;
}

static int
_write(lua_State *L) {
	int fd = luaL_checkinteger(L,1);
	int type = lua_type(L,2);
	const char * buffer;
	size_t sz;
	if (type == LUA_TSTRING) {
		buffer = lua_tolstring(L,2,&sz);
	} else if (type == LUA_TLIGHTUSERDATA) {
		buffer = lua_touserdata(L,2);
		sz = luaL_checkinteger(L,3);
	}
	for (;;) {
		int err = send(fd, buffer, sz, 0);
		if (err < 0) {
			switch (errno) {
			case EAGAIN:
			case EINTR:
				continue;
			}
		}
		assert(err == sz);
		return 0;
	}
}

struct buffer {
	int cap;
	int size;
	char * buffer;
	int read;
};

static int
_new(lua_State *L) {
	struct buffer * buffer = lua_newuserdata(L, sizeof(struct buffer));
	buffer->cap = 0;
	buffer->size = 0;
	buffer->buffer = NULL;
	buffer->read = 0;
	return 1;
}

static int
_delete(lua_State *L) {
	struct buffer * buffer = lua_touserdata(L,1);
	free(buffer->buffer);
	buffer->buffer = NULL;
	return 0;
}

static int
_push(lua_State *L) {
	luaL_checktype(L,1,LUA_TUSERDATA);
	struct buffer * buffer = lua_touserdata(L,1);
	int type = lua_type(L,2);
	const char * buf;
	size_t size;
	if (type == LUA_TSTRING) {
		buf = lua_tolstring(L,2,&size);
	} else {
		luaL_checktype(L,2,LUA_TLIGHTUSERDATA);
		buf = lua_touserdata(L,2);
		size = luaL_checkinteger(L,3);
	}
	int old_size = buffer->size - buffer->read;
	if (size + old_size > buffer->cap) {
		if (buffer->cap == 0) {
			lua_rawgetp(L, LUA_REGISTRYINDEX, _delete);
			lua_setmetatable(L, 1);
		}

		if (buffer->read == 0) {
			buffer->buffer = realloc(buffer->buffer, size+old_size);
			memcpy(buffer->buffer + old_size , buf , size);
			buffer->size += size;
		} else {
			char * new_buffer = malloc(size + old_size);
			memcpy(new_buffer, buffer->buffer + buffer->read, old_size);
			memcpy(new_buffer + old_size , buf, size);
			free(buffer->buffer);
			buffer->buffer = new_buffer;
			buffer->read = 0;
			buffer->size = old_size + size;
		}
	} else if (buffer->size + size > buffer->cap) {
		memmove(buffer->buffer, buffer->buffer + buffer->read, old_size);
		memcpy(buffer->buffer + old_size, buf, size);
		buffer->read = 0;
		buffer->size = old_size + size;
	} else {
		memcpy(buffer->buffer + buffer->size, buf, size);
		buffer->size += size;
	}

	return 0;
}

static int
_read(lua_State *L) {
	luaL_checktype(L,1,LUA_TUSERDATA);
	struct buffer * buffer = lua_touserdata(L,1);
	int need = luaL_checkinteger(L,2);
	int size = buffer->size - buffer->read;
	if (need <= size) {
		lua_pushlstring(L, buffer->buffer + buffer->read, need);
		buffer->read += need;
		return 1;
	}
	return 0;
}

static int
_readline(lua_State *L) {
	luaL_checktype(L,1,LUA_TUSERDATA);
	struct buffer * buffer = lua_touserdata(L,1);
	size_t sz;
	const char * sep = luaL_checklstring(L,2,&sz);
	int i;
	for (i=buffer->read;i<=buffer->size - (int)sz;i++) {
		if (memcmp(buffer->buffer+i,sep,sz) == 0) {
			lua_pushlstring(L, buffer->buffer + buffer->read, i-buffer->read);
			buffer->read = i+sz;
			return 1;
		}
	}
	return 0;
}

int
luaopen_socket_c(lua_State *L) {
	luaL_Reg l[] = {
		{ "open", _open },
		{ "close", _close },
		{ "write", _write },
		{ "new", _new },
		{ "push", _push },
		{ "read", _read },
		{ "readline", _readline },
		{ NULL, NULL },
	};
	luaL_checkversion(L);
	luaL_newlib(L,l);
	lua_createtable(L,0,1);
	lua_pushcfunction(L, _delete);
	lua_setfield(L, -2, "__gc");
	lua_rawsetp(L, LUA_REGISTRYINDEX, _delete);
	return 1;
}

