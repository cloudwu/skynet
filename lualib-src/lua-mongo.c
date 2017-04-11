#define LUA_LIB

#include "skynet_malloc.h"

#include <lua.h>
#include <lauxlib.h>

#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#define OP_REPLY 1
#define OP_MSG	1000
#define OP_UPDATE 2001
#define OP_INSERT 2002
#define OP_QUERY 2004
#define OP_GET_MORE	2005
#define OP_DELETE 2006
#define OP_KILL_CURSORS	2007

#define REPLY_CURSORNOTFOUND 1
#define REPLY_QUERYFAILURE 2
#define REPLY_AWAITCAPABLE 8	// ignore because mongo 1.6+ always set it

#define DEFAULT_CAP 128

struct connection {
	int sock;
	int id;
};

struct response {
	int flags;
	int32_t cursor_id[2];
	int starting_from;
	int number;
};

struct buffer {
	int size;
	int cap;
	uint8_t * ptr;
	uint8_t buffer[DEFAULT_CAP];
};

static inline uint32_t
little_endian(uint32_t v) {
	union {
		uint32_t v;
		uint8_t b[4];
	} u;
	u.v = v;
	return u.b[0] | u.b[1] << 8 | u.b[2] << 16 | u.b[3] << 24;
}

typedef void * document;

static inline uint32_t
get_length(document buffer) {
	union {
		uint32_t v;
		uint8_t b[4];
	} u;
	memcpy(&u.v, buffer, 4);
	return u.b[0] | u.b[1] << 8 | u.b[2] << 16 | u.b[3] << 24;
}

static inline void
buffer_destroy(struct buffer *b) {
	if (b->ptr != b->buffer) {
		skynet_free(b->ptr);
	}
}

static inline void
buffer_create(struct buffer *b) {
	b->size = 0;
	b->cap = DEFAULT_CAP;
	b->ptr = b->buffer;
}

static inline void
buffer_reserve(struct buffer *b, int sz) {
	if (b->size + sz <= b->cap)
		return;
	do {
		b->cap *= 2;
	} while (b->cap <= b->size + sz);

	if (b->ptr == b->buffer) {
		b->ptr = skynet_malloc(b->cap);
		memcpy(b->ptr, b->buffer, b->size);
	} else {
		b->ptr = skynet_realloc(b->ptr, b->cap);
	}
}

static inline void
write_int32(struct buffer *b, int32_t v) {
	uint32_t uv = (uint32_t)v;
	buffer_reserve(b,4);
	b->ptr[b->size++] = uv & 0xff;
	b->ptr[b->size++] = (uv >> 8)&0xff;
	b->ptr[b->size++] = (uv >> 16)&0xff;
	b->ptr[b->size++] = (uv >> 24)&0xff;
}

static inline void
write_bytes(struct buffer *b, const void * buf, int sz) {
	buffer_reserve(b,sz);
	memcpy(b->ptr + b->size, buf, sz);
	b->size += sz;
}

static void
write_string(struct buffer *b, const char *key, size_t sz) {
	buffer_reserve(b,sz+1);
	memcpy(b->ptr + b->size, key, sz);
	b->ptr[b->size+sz] = '\0';
	b->size+=sz+1;
}

static inline int
reserve_length(struct buffer *b) {
	int sz = b->size;
	buffer_reserve(b,4);
	b->size +=4;
	return sz;
}

static inline void
write_length(struct buffer *b, int32_t v, int off) {
	uint32_t uv = (uint32_t)v;
	b->ptr[off++] = uv & 0xff;
	b->ptr[off++] = (uv >> 8)&0xff;
	b->ptr[off++] = (uv >> 16)&0xff;
	b->ptr[off++] = (uv >> 24)&0xff;
}

// 1 integer id
// 2 integer flags
// 3 string collection name
// 4 integer skip
// 5 integer return number
// 6 document query
// 7 document selector (optional)
// return string package
static int
op_query(lua_State *L) {
	int id = luaL_checkinteger(L,1);
	document query = lua_touserdata(L,6);
	if (query == NULL) {
		return luaL_error(L, "require query document");
	}
	document selector = lua_touserdata(L,7);
	int flags = luaL_checkinteger(L, 2);
	size_t nsz = 0;
	const char *name = luaL_checklstring(L,3,&nsz);
	int skip = luaL_checkinteger(L, 4);
	int number = luaL_checkinteger(L, 5);

	luaL_Buffer b;
	luaL_buffinit(L,&b);

	struct buffer buf;
	buffer_create(&buf);
		int len = reserve_length(&buf);
		write_int32(&buf, id);
		write_int32(&buf, 0);
		write_int32(&buf, OP_QUERY);
		write_int32(&buf, flags);
		write_string(&buf, name, nsz);
		write_int32(&buf, skip);
		write_int32(&buf, number);

		int32_t query_len = get_length(query);
		int total = buf.size + query_len;
		int32_t selector_len = 0;
		if (selector) {
			selector_len = get_length(selector);
			total += selector_len;
		}

		write_length(&buf, total, len);
		luaL_addlstring(&b, (const char *)buf.ptr, buf.size);
	buffer_destroy(&buf);

	luaL_addlstring(&b, (const char *)query, query_len);

	if (selector) {
		luaL_addlstring(&b, (const char *)selector, selector_len);
	}

	luaL_pushresult(&b);

	return 1;
}

// 1 string data
// 2 result document table
// return boolean succ (false -> request id, error document)
//	number request_id
//  document first
//	string cursor_id
//  integer startfrom
static int
op_reply(lua_State *L) {
	size_t data_len = 0;
	const char * data = luaL_checklstring(L,1,&data_len);
	struct {
//		int32_t length; // total message size, including this
		int32_t request_id; // identifier for this message
		int32_t response_id; // requestID from the original request
							// (used in reponses from db)
		int32_t opcode; // request type 
		int32_t flags;
		int32_t cursor_id[2];
		int32_t starting;
		int32_t number;
	} const *reply = (const void *)data;

	if (data_len < sizeof(*reply)) {
		lua_pushboolean(L, 0);
		return 1;
	}

	int id = little_endian(reply->response_id);
	int flags = little_endian(reply->flags);
	if (flags & REPLY_QUERYFAILURE) {
		lua_pushboolean(L,0);
		lua_pushinteger(L, id);
		lua_pushlightuserdata(L, (void *)(reply+1));
		return 3;
	}

	int starting_from = little_endian(reply->starting);
	int number = little_endian(reply->number);
	int sz = (int)data_len - sizeof(*reply);
	const uint8_t * doc = (const uint8_t *)(reply+1);

	if (lua_istable(L,2)) {
		int i = 1;
		while (sz > 4) {
			lua_pushlightuserdata(L, (void *)doc);
			lua_rawseti(L, 2, i);

			int32_t doc_len = get_length((document)doc);

			doc += doc_len;
			sz -= doc_len;

			++i;
		}
		if (i != number + 1) {
			lua_pushboolean(L,0);
			lua_pushinteger(L, id);
			return 2;
		}
		int c = lua_rawlen(L, 2);
		for (;i<=c;i++) {
			lua_pushnil(L);
			lua_rawseti(L, 2, i);
		}
	} else {
		if (sz >= 4) {
			sz -= get_length((document)doc);
		}
	}
	if (sz != 0) {
		return luaL_error(L, "Invalid result bson document");
	}
	lua_pushboolean(L,1);
	lua_pushinteger(L, id);
	if (number == 0)
		lua_pushnil(L);
	else
		lua_pushlightuserdata(L, (void *)(reply+1));
	if (reply->cursor_id[0] == 0 && reply->cursor_id[1]==0) {
		// closed cursor
		lua_pushnil(L);
	} else {
		lua_pushlstring(L, (const char *)(reply->cursor_id), 8);
	}
	lua_pushinteger(L, starting_from);

	return 5;
}

/*
	1 string cursor_id
	return string package
 */
static int
op_kill(lua_State *L) {
	size_t cursor_len = 0;
	const char * cursor_id = luaL_tolstring(L, 1, &cursor_len);
	if (cursor_len != 8) {
		return luaL_error(L, "Invalid cursor id");
	}

	struct buffer buf;
	buffer_create(&buf);

	int len = reserve_length(&buf);
	write_int32(&buf, 0);
	write_int32(&buf, 0);
	write_int32(&buf, OP_KILL_CURSORS);

	write_int32(&buf, 0);
	write_int32(&buf, 1);
	write_bytes(&buf, cursor_id, 8);

	write_length(&buf, buf.size, len);

	lua_pushlstring(L, (const char *)buf.ptr, buf.size);
	buffer_destroy(&buf);

	return 1;
}

/*
	1 string collection
	2 integer single remove
	3 document selector

	return string package
 */
static int
op_delete(lua_State *L) {
	document selector  = lua_touserdata(L,3);
	if (selector == NULL) {
		luaL_error(L, "Invalid param");
	}
	size_t sz = 0;
	const char * name = luaL_checklstring(L,1,&sz);

	luaL_Buffer b;
	luaL_buffinit(L,&b);

	struct buffer buf;
	buffer_create(&buf);
		int len = reserve_length(&buf);
		write_int32(&buf, 0);
		write_int32(&buf, 0);
		write_int32(&buf, OP_DELETE);
		write_int32(&buf, 0);
		write_string(&buf, name, sz);
		write_int32(&buf, lua_tointeger(L,2));

		int32_t selector_len = get_length(selector);
		int total = buf.size + selector_len;
		write_length(&buf, total, len);

		luaL_addlstring(&b, (const char *)buf.ptr, buf.size);
	buffer_destroy(&buf);

	luaL_addlstring(&b, (const char *)selector, selector_len);
	luaL_pushresult(&b);

	return 1;
}

/*
	1 integer id
	2 string collection
	3 integer number
	4 cursor_id (8 bytes string/ 64bit)

	return string package
 */
static int
op_get_more(lua_State *L) {
	int id = luaL_checkinteger(L, 1);
	size_t sz = 0;
	const char * name = luaL_checklstring(L,2,&sz);
	int number = luaL_checkinteger(L, 3);
	size_t cursor_len = 0;
	const char * cursor_id = luaL_tolstring(L, 4, &cursor_len);
	if (cursor_len != 8) {
		return luaL_error(L, "Invalid cursor id");
	}

	struct buffer buf;
	buffer_create(&buf);
		int len = reserve_length(&buf);
		write_int32(&buf, id);
		write_int32(&buf, 0);
		write_int32(&buf, OP_GET_MORE);
		write_int32(&buf, 0);
		write_string(&buf, name, sz);
		write_int32(&buf, number);
		write_bytes(&buf, cursor_id, 8);
		write_length(&buf, buf.size, len);

		lua_pushlstring(L, (const char *)buf.ptr, buf.size);
	buffer_destroy(&buf);

	return 1;
}

// 1 string collection
// 2 integer flags
// 3 document selector
// 4 document update
// return string package
static int
op_update(lua_State *L) {
	document selector  = lua_touserdata(L,3);
	document update = lua_touserdata(L,4);
	if (selector == NULL || update == NULL) {
		luaL_error(L, "Invalid param");
	}
	size_t sz = 0;
	const char * name = luaL_checklstring(L,1,&sz);

	luaL_Buffer b;
	luaL_buffinit(L, &b);

	struct buffer buf;
	buffer_create(&buf);
		// make package header, don't raise L error
		int len = reserve_length(&buf);
		write_int32(&buf, 0);
		write_int32(&buf, 0);
		write_int32(&buf, OP_UPDATE);
		write_int32(&buf, 0);
		write_string(&buf, name, sz);
		write_int32(&buf, lua_tointeger(L,2));

		int32_t selector_len = get_length(selector);
		int32_t update_len = get_length(update);

		int total = buf.size + selector_len + update_len;
		write_length(&buf, total, len);

		luaL_addlstring(&b, (const char *)buf.ptr, buf.size);
	buffer_destroy(&buf);

	luaL_addlstring(&b, (const char *)selector, selector_len);
	luaL_addlstring(&b, (const char *)update, update_len);

	luaL_pushresult(&b);

	return 1;
}

static int
document_length(lua_State *L) {
	if (lua_isuserdata(L, 3)) {
		document doc = lua_touserdata(L,3);
		return get_length(doc);
	}
	if (lua_istable(L,3)) {
		int total = 0;
		int s = lua_rawlen(L,3);
		int i;
		for (i=1;i<=s;i++) {
			lua_rawgeti(L, 3, i);
			document doc = lua_touserdata(L,-1);
			if (doc == NULL) {
				lua_pop(L,1);
				return luaL_error(L, "Invalid document at %d", i);
			} else {
				total += get_length(doc);
				lua_pop(L,1);
			}
		}
		return total;
	}
	return luaL_error(L, "Insert need documents");
}

// 1 integer flags
// 2 string collection
// 3 documents
// return string package
static int
op_insert(lua_State *L) {
	size_t sz = 0;
	const char * name = luaL_checklstring(L,2,&sz);
	int dsz = document_length(L);

	luaL_Buffer b;
	luaL_buffinit(L, &b);

	struct buffer buf;
	buffer_create(&buf);
		// make package header, don't raise L error
		int len = reserve_length(&buf);
		write_int32(&buf, 0);
		write_int32(&buf, 0);
		write_int32(&buf, OP_INSERT);
		write_int32(&buf, lua_tointeger(L,1));
		write_string(&buf, name, sz);

		int total = buf.size + dsz;
		write_length(&buf, total, len);

		luaL_addlstring(&b, (const char *)buf.ptr, buf.size);
	buffer_destroy(&buf);
	
	if (lua_isuserdata(L,3)) {
		document doc = lua_touserdata(L,3);
		luaL_addlstring(&b, (const char *)doc, get_length(doc));
	} else {
		int s = lua_rawlen(L, 3);
		int i;
		for (i=1;i<=s;i++) {
			lua_rawgeti(L,3,i);
			document doc = lua_touserdata(L,-1);
			lua_pop(L,1);	// must call lua_pop before luaL_addlstring, because addlstring may change stack top
			luaL_addlstring(&b, (const char *)doc, get_length(doc));
		}
	}

	luaL_pushresult(&b);

	return 1;
}

// string 4 bytes length
// return integer
static int
reply_length(lua_State *L) {
	const char * rawlen_str = luaL_checkstring(L, 1);
	int rawlen = 0;
	memcpy(&rawlen, rawlen_str, sizeof(int));
	int length = little_endian(rawlen);
	lua_pushinteger(L, length - 4);
	return 1;
}

LUAMOD_API int
luaopen_mongo_driver(lua_State *L) {
	luaL_checkversion(L);
	luaL_Reg l[] ={
		{ "query", op_query },
		{ "reply", op_reply },
		{ "kill", op_kill },
		{ "delete", op_delete },
		{ "more", op_get_more },
		{ "update", op_update },
		{ "insert", op_insert },
		{ "length", reply_length },
		{ NULL, NULL },
	};

	luaL_newlib(L,l);
	return 1;
}
