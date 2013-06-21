#include <stdlib.h>
#include <string.h>

#include <lua.h>
#include <lauxlib.h>

#include "luacompat52.h"

struct socket_buffer {
	int size;
	int head;
	int tail;
};

static struct socket_buffer *
new_buffer(lua_State *L, int sz) {
	struct socket_buffer * buffer = lua_newuserdata(L, sizeof(*buffer) + sz);
	buffer->size = sz;
	buffer->head = 0;
	buffer->tail = 0;

	return buffer;
}

static void
copy_buffer(struct socket_buffer * dest, struct socket_buffer * src) {
	char * ptr = (char *)(src+1);
	if (src->tail >= src->head) {
		int sz = src->tail - src->head;
		memcpy(dest+1, ptr+ src->head, sz);
		dest->tail = sz;
	} else {
		char * d = (char *)(dest+1);
		int part = src->size - src->head;
		memcpy(d, ptr + src->head, part);
		memcpy(d + part, ptr , src->tail);
		dest->tail = src->tail + part;
	}
}

static void
append_buffer(struct socket_buffer * buffer, char * msg, int sz) {
	char * dst = (char *)(buffer + 1);
	if (sz + buffer->tail < buffer->size) {
		memcpy(dst + buffer->tail , msg, sz);
		buffer->tail += sz;
	} else {
		int part = buffer->size - buffer->tail;
		memcpy(dst + buffer->tail , msg, part);
		memcpy(dst, msg + part, sz - part);
		buffer->tail = sz - part;
	}
}

static int
lpop(lua_State *L) {
	struct socket_buffer * buffer = lua_touserdata(L, 1);
	if (buffer == NULL) {
		return 0;
	}
	int sz = luaL_checkinteger(L, 2);
	int	bytes = buffer->tail - buffer->head;
	if (bytes < 0) {
		bytes += buffer->size;
	}

	if (sz > bytes || bytes == 0) {
		lua_pushnil(L);
		lua_pushinteger(L, bytes);
		return 2;
	}

	if (sz == 0) {
		sz = bytes;
	}

	char * ptr = (char *)(buffer+1);
	if (buffer->size - buffer->head >=sz) {
		lua_pushlstring(L, ptr + buffer->head, sz);
		buffer->head+=sz;
	} else {
		luaL_Buffer b;
		luaL_buffinit(L, &b);
		luaL_addlstring(&b, ptr + buffer->head, buffer->size - buffer->head);
		buffer->head = sz - (buffer->size - buffer->head);
		luaL_addlstring(&b, ptr, buffer->head);
		luaL_pushresult(&b);
	}

	bytes -= sz;

	lua_pushinteger(L,bytes);
	if (bytes == 0) {
		buffer->head = buffer->tail = 0;
	}
	
	return 2;
}

static inline int
check_sep(struct socket_buffer *buffer, int from, const char * sep, int sz) {
	const char * ptr = (const char *)(buffer+1);
	int i;
	for (i=0;i<sz;i++) {
		int index = from + i;
		if (index >= buffer->size) {
			index = 0;
		}
		if (ptr[index] != sep[i]) {
			return 0;
		}
	}
	return 1;
}

static int
lreadline(lua_State *L) {
	struct socket_buffer * buffer = lua_touserdata(L, 1);
	if (buffer == NULL) {
		return 0;
	}
	size_t len = 0;
	const char *sep = luaL_checklstring(L,2,&len);
	int read = !lua_toboolean(L,3);
	int	bytes = buffer->tail - buffer->head;

	if (bytes < 0) {
		bytes += buffer->size;
	}

	int i;
	for (i=0;i<=bytes-(int)len;i++) {
		int index = buffer->head + i;
		if (index >= buffer->size) {
			index -= buffer->size;
		}
		if (check_sep(buffer, index, sep, (int)len)) {
			if (read == 0) {
				lua_pushboolean(L,1);
			} else {
				if (i==0) {
					lua_pushlstring(L, "", 0);
				} else {
					const char * ptr = (const char *)(buffer+1);
					if (--index < 0) {
						index = buffer->size -1;
					}
					if (index < buffer->head) {
						luaL_Buffer b;
						luaL_buffinit(L, &b);
						luaL_addlstring(&b, ptr + buffer->head, buffer->size-buffer->head);
						luaL_addlstring(&b, ptr, index+1);
						luaL_pushresult(&b);
					} else {
						lua_pushlstring(L, ptr + buffer->head, index-buffer->head+1);
					}
					++index;
				}
				index+=len;
				if (index >= buffer->size) {
					index-=buffer->size;
				}
				buffer->head = index;
			}
			return 1;
		}
	}
	return 0;
}

static int
lpush(lua_State *L) {
	struct socket_buffer * buffer = lua_touserdata(L, 1);
	int bytes = 0;
	if (buffer) {
		bytes = buffer->tail - buffer->head;
		if (bytes < 0) {
			bytes += buffer->size;
		}
	}
	void * msg = lua_touserdata(L,2);
	if (msg == NULL) {
		lua_settop(L,1);
		lua_pushinteger(L,bytes);
		return 2;
	}

	int sz = luaL_checkinteger(L,3);

	if (buffer == NULL) {
		struct socket_buffer * nbuf = new_buffer(L, sz * 2);
		append_buffer(nbuf, msg, sz);
	} else if (sz + bytes >= buffer->size) {
		struct socket_buffer * nbuf = new_buffer(L, (sz + bytes) * 2);
		copy_buffer(nbuf, buffer);
		append_buffer(nbuf, msg, sz);
	} else {
		lua_settop(L,1);
		append_buffer(buffer, msg, sz);
	}
	lua_pushinteger(L, sz + bytes);
	return 2;
}

static int
lunpack(lua_State *L) {
	int * msg = lua_touserdata(L,1);
	int sz = luaL_checkinteger(L,2);
	if (msg == NULL || sz < 4) {
		return luaL_error(L, "Invalid socket message");
	}
	lua_pushinteger(L, *msg);
	lua_pushlightuserdata(L, msg+1);
	lua_pushinteger(L, sz - 4);
	return 3;
}

static int
lpack(lua_State *L) {
	int fd = luaL_checkinteger(L, 1);
	const void *buffer;
	size_t sz;
	if (lua_isuserdata(L,2)) {
		buffer = lua_touserdata(L,2);
		sz = luaL_checkinteger(L, 3);
	} else {
		buffer = luaL_checklstring(L,2,&sz);
	}
	int * b = malloc(4 + sz);
	*b = fd;
	memcpy(b+1, buffer, sz);
	lua_pushlightuserdata(L, b);
	lua_pushinteger(L, 4+sz);
	return 2;
}

int
luaopen_socketbuffer(lua_State *L) {
	luaL_checkversion(L);
	luaL_Reg l[] = {
		{ "push", lpush },
		{ "pop", lpop },
		{ "readline", lreadline },
		{ "unpack", lunpack },
		{ "pack", lpack },
		{ NULL, NULL },
	};
	luaL_newlib(L,l);
	return 1;
}
