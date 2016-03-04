#include <lua.h>
#include <lauxlib.h>

#include <time.h>
#include <unistd.h>

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include "atomic.h"

#define DEFAULT_CAP 64
#define MAX_NUMBER 1024
// avoid circular reference while encodeing
#define MAX_DEPTH 128

#define BSON_REAL 1
#define BSON_STRING 2
#define BSON_DOCUMENT 3
#define BSON_ARRAY 4
#define BSON_BINARY 5
#define BSON_UNDEFINED 6
#define BSON_OBJECTID 7
#define BSON_BOOLEAN 8
#define BSON_DATE 9
#define BSON_NULL 10
#define BSON_REGEX 11
#define BSON_DBPOINTER 12
#define BSON_JSCODE 13
#define BSON_SYMBOL 14
#define BSON_CODEWS 15
#define BSON_INT32 16
#define BSON_TIMESTAMP 17
#define BSON_INT64 18
#define BSON_MINKEY 255
#define BSON_MAXKEY 127

#define BSON_TYPE_SHIFT 5

static char bson_numstrs[MAX_NUMBER][4];
static int bson_numstr_len[MAX_NUMBER];

struct bson {
	int size;
	int cap;
	uint8_t *ptr;
	uint8_t buffer[DEFAULT_CAP];
};

struct bson_reader {
	const uint8_t * ptr;
	int size;
};

static inline int32_t
get_length(const uint8_t * data) {
	const uint8_t * b = (const uint8_t *)data;
	int32_t len = b[0] | b[1]<<8 | b[2]<<16 | b[3]<<24;
	return len;
}

static inline void
bson_destroy(struct bson *b) {
	if (b->ptr != b->buffer) {
		free(b->ptr);
	}
}

static inline void
bson_create(struct bson *b) {
	b->size = 0;
	b->cap = DEFAULT_CAP;
	b->ptr = b->buffer;
}

static inline void
bson_reserve(struct bson *b, int sz) {
	if (b->size + sz <= b->cap)
		return;
	do {
		b->cap *= 2;
	} while (b->cap <= b->size + sz);

	if (b->ptr == b->buffer) {
		b->ptr = malloc(b->cap);
		memcpy(b->ptr, b->buffer, b->size);
	} else {
		b->ptr = realloc(b->ptr, b->cap);
	}
}

static inline void
check_reader(lua_State *L, struct bson_reader *br, int sz) {
	if (br->size < sz) {
		luaL_error(L, "Invalid bson block (%d:%d)", br->size, sz);
	}
}

static inline int
read_byte(lua_State *L, struct bson_reader *br) {
	check_reader(L, br, 1);
	const uint8_t * b = br->ptr;
	int r =  b[0];
	++br->ptr;
	--br->size;

	return r;
}

static inline int32_t
read_int32(lua_State *L, struct bson_reader *br) {
	check_reader(L, br, 4);
	const uint8_t * b = br->ptr;
	uint32_t v = b[0] | b[1]<<8 | b[2]<<16 | b[3]<<24;
	br->ptr+=4;
	br->size-=4;
	return (int32_t)v;
}

static inline int64_t
read_int64(lua_State *L, struct bson_reader *br) {
	check_reader(L, br, 8);
	const uint8_t * b = br->ptr;
	uint32_t lo = b[0] | b[1]<<8 | b[2]<<16 | b[3]<<24;
	uint32_t hi = b[4] | b[5]<<8 | b[6]<<16 | b[7]<<24;
	uint64_t v = (uint64_t)lo | (uint64_t)hi<<32;
	br->ptr+=8;
	br->size-=8;
	return (int64_t)v;
}

static inline lua_Number
read_double(lua_State *L, struct bson_reader *br) {
	check_reader(L, br, 8);
	union {
		uint64_t i;
		double d;
	} v;
	const uint8_t * b = br->ptr;
	uint32_t lo = b[0] | b[1]<<8 | b[2]<<16 | b[3]<<24;
	uint32_t hi = b[4] | b[5]<<8 | b[6]<<16 | b[7]<<24;
	v.i = (uint64_t)lo | (uint64_t)hi<<32;
	br->ptr+=8;
	br->size-=8;
	return v.d;
}

static inline const void *
read_bytes(lua_State *L, struct bson_reader *br, int sz) {
	const void * r = br->ptr;
	check_reader(L, br, sz);
	br->ptr+=sz;
	br->size-=sz;
	return r;
}

static inline const char *
read_cstring(lua_State *L, struct bson_reader *br, size_t *sz) {
	int i;
	for (i=0;;i++) {
		if (i==br->size) {
			luaL_error(L, "Invalid bson block : cstring");
		}
		if (br->ptr[i] == '\0') {
			break;
		}
	}
	*sz = i;
	const char * r = (const char *)br->ptr;
	br->ptr += i+1;
	br->size -= i+1;
	return r;
}

static inline void
write_byte(struct bson *b, uint8_t v) {
	bson_reserve(b,1);
	b->ptr[b->size++] = v;
}

static inline void
write_int32(struct bson *b, int32_t v) {
	uint32_t uv = (uint32_t)v;
	bson_reserve(b,4);
	b->ptr[b->size++] = uv & 0xff;
	b->ptr[b->size++] = (uv >> 8)&0xff;
	b->ptr[b->size++] = (uv >> 16)&0xff;
	b->ptr[b->size++] = (uv >> 24)&0xff;
}

static inline void
write_length(struct bson *b, int32_t v, int off) {
	uint32_t uv = (uint32_t)v;
	b->ptr[off++] = uv & 0xff;
	b->ptr[off++] = (uv >> 8)&0xff;
	b->ptr[off++] = (uv >> 16)&0xff;
	b->ptr[off++] = (uv >> 24)&0xff;
}

static void
write_string(struct bson *b, const char *key, size_t sz) {
	bson_reserve(b,sz+1);
	memcpy(b->ptr + b->size, key, sz);
	b->ptr[b->size+sz] = '\0';
	b->size+=sz+1;
}

static inline int
reserve_length(struct bson *b) {
	int sz = b->size;
	bson_reserve(b,4);
	b->size +=4;
	return sz;
}

static inline void
write_int64(struct bson *b, int64_t v) {
	uint64_t uv = (uint64_t)v;
	int i;
	bson_reserve(b,8);
	for (i=0;i<64;i+=8) {
		b->ptr[b->size++] = (uv>>i) & 0xff;
	}
}

static inline void
write_double(struct bson *b, lua_Number d) {
	union {
		double d;
		uint64_t i;
	} v;
	v.d = d;
	int i;
	bson_reserve(b,8);
	for (i=0;i<64;i+=8) {
		b->ptr[b->size++] = (v.i>>i) & 0xff;
	}
}

static void pack_dict(lua_State *L, struct bson *b, bool array, int depth);

static inline void
append_key(struct bson *bs, int type, const char *key, size_t sz) {
	write_byte(bs, type);
	write_string(bs, key, sz);
}

static inline int
is_32bit(int64_t v) {
	return v >= INT32_MIN && v <= INT32_MAX;
}

static void
append_number(struct bson *bs, lua_State *L, const char *key, size_t sz) {
	if (lua_isinteger(L, -1)) {
		int64_t i = lua_tointeger(L, -1);
		if (is_32bit(i)) {
			append_key(bs, BSON_INT32, key, sz);
			write_int32(bs, i);
		} else {
			append_key(bs, BSON_INT64, key, sz);
			write_int64(bs, i);
		}
	} else {
		lua_Number d = lua_tonumber(L,-1);
		append_key(bs, BSON_REAL, key, sz);
		write_double(bs, d);
	}
}

static void
append_table(struct bson *bs, lua_State *L, const char *key, size_t sz, int depth) {
	size_t len = lua_rawlen(L, -1);
	bool isarray = false;
	if (len > 0) {
		lua_pushinteger(L, len);
		if (lua_next(L,-2) == 0) {
			isarray = true;
		} else {
			lua_pop(L,2);
		}
	}
	if (isarray) {
		append_key(bs, BSON_ARRAY, key, sz);
	} else {
		append_key(bs, BSON_DOCUMENT, key, sz);
	}
	pack_dict(L, bs, isarray, depth);
}

static void
write_binary(struct bson *b, const void * buffer, size_t sz) {
	int length = reserve_length(b);
	bson_reserve(b,sz);
	memcpy(b->ptr + b->size, buffer, sz);	// include sub type
	b->size+=sz;
	write_length(b, sz-1, length);	// not include sub type
}

static void
append_one(struct bson *bs, lua_State *L, const char *key, size_t sz, int depth) {
	int vt = lua_type(L,-1);
	switch(vt) {
	case LUA_TNUMBER:
		append_number(bs, L, key, sz);
		break;
	case LUA_TUSERDATA: {
		append_key(bs, BSON_DOCUMENT, key, sz);
		int32_t * doc = lua_touserdata(L,-1);
		int32_t sz = *doc;
		bson_reserve(bs,sz);
		memcpy(bs->ptr + bs->size, doc, sz);
		bs->size += sz;
		break;
	}
	case LUA_TSTRING: {
		size_t len;
		const char * str = lua_tolstring(L,-1,&len);
		if (len > 1 && str[0]==0) {
			int subt = (uint8_t)str[1];
			append_key(bs, subt, key, sz);
			switch(subt) {
			case BSON_BINARY:
				write_binary(bs, str+2, len-2);
				break;
			case BSON_OBJECTID:
				if (len != 2+12) {
					luaL_error(L, "Invalid object id %s", str+2);
				}
				// go though
			case BSON_JSCODE:
			case BSON_DBPOINTER:
			case BSON_SYMBOL:
			case BSON_CODEWS:
				bson_reserve(bs,len-2);
				memcpy(bs->ptr + bs->size, str+2, len-2);
				bs->size += len-2;
				break;
			case BSON_DATE: {
				if (len != 2+4) {
					luaL_error(L, "Invalid date");
				}
				const uint32_t * ts = (const uint32_t *)(str + 2);
				int64_t v = (int64_t)*ts * 1000;
				write_int64(bs, v);
				break;
			}
			case BSON_TIMESTAMP: {
				if (len != 2+8) {
					luaL_error(L, "Invalid timestamp");
				}
				const uint32_t * inc = (const uint32_t *)(str + 2);
				const uint32_t * ts = (const uint32_t *)(str + 6);
				write_int32(bs, *inc);
				write_int32(bs, *ts);
				break;
			}
			case BSON_REGEX: {
				str+=2;
				len-=3;
				size_t i;
				for (i=0;i<len;i++) {
					if (str[len-i-1]==0) {
						break;
					}
				}
				write_string(bs, str, len-i-1);
				write_string(bs, str + len-i, i);
				break;
			}
			case BSON_MINKEY:
			case BSON_MAXKEY:
			case BSON_NULL:
				break;
			default:
				luaL_error(L,"Invalid subtype %d", subt);
			}
		} else {
			size_t len;
			const char * str = lua_tolstring(L,-1,&len);
			append_key(bs, BSON_STRING, key, sz);
			int off = reserve_length(bs);
			write_string(bs, str, len);
			write_length(bs, len+1, off);		
		}
		break;
	}
	case LUA_TTABLE:
		append_table(bs, L, key, sz, depth+1);
		break;
	case LUA_TBOOLEAN:
		append_key(bs, BSON_BOOLEAN, key, sz);
		write_byte(bs, lua_toboolean(L,-1));
		break;
	default:
		luaL_error(L, "Invalid value type : %s", lua_typename(L,vt));
	}
}

static inline int 
bson_numstr( char *str, unsigned int i ) {
	if ( i < MAX_NUMBER) {
		memcpy( str, bson_numstrs[i], 4 );
		return bson_numstr_len[i];
	} else {
		return sprintf( str,"%u", i );
	}
}

static void
pack_dict(lua_State *L, struct bson *b, bool isarray, int depth) {
	if (depth > MAX_DEPTH) {
		luaL_error(L, "Too depth while encoding bson");
	}
	luaL_checkstack(L, 16, NULL);	// reserve enough stack space to pack table
	int length = reserve_length(b);
	lua_pushnil(L);
	while(lua_next(L,-2) != 0) {
		int kt = lua_type(L, -2);
		char numberkey[32];
		const char * key = NULL;
		size_t sz;
		if (isarray) {
			if (kt != LUA_TNUMBER) {
				luaL_error(L, "Invalid array key type : %s", lua_typename(L, kt));
				return;
			}
			sz = bson_numstr(numberkey, (unsigned int)lua_tointeger(L,-2)-1);
			key = numberkey;

			append_one(b, L, key, sz, depth);
			lua_pop(L,1);
		} else {
			switch(kt) {
			case LUA_TNUMBER:
				// copy key, don't change key type
				lua_pushvalue(L,-2);
				lua_insert(L,-2);
				key = lua_tolstring(L,-2,&sz);
				append_one(b, L, key, sz, depth);
				lua_pop(L,2);
				break;
			case LUA_TSTRING:
				key = lua_tolstring(L,-2,&sz);
				append_one(b, L, key, sz, depth);
				lua_pop(L,1);
				break;
			default:
				luaL_error(L, "Invalid key type : %s", lua_typename(L, kt));
				return;
			}
		}
	}
	write_byte(b,0);
	write_length(b, b->size - length, length);
}

static void
pack_ordered_dict(lua_State *L, struct bson *b, int n, int depth) {
	int length = reserve_length(b);
	int i;
	for (i=0;i<n;i+=2) {
		size_t sz;
		const char * key = lua_tolstring(L, i+1, &sz);
		if (key == NULL) {
			luaL_error(L, "Argument %d need a string", i+1);
		}
		lua_pushvalue(L, i+2);
		append_one(b, L, key, sz, depth);
		lua_pop(L,1);
	}
	write_byte(b,0);
	write_length(b, b->size - length, length);
}
 
static int
ltostring(lua_State *L) {
	size_t sz = lua_rawlen(L, 1);
	void * ud = lua_touserdata(L,1);
	lua_pushlstring(L, ud, sz);
	return 1;
}

static int
llen(lua_State *L) {
	size_t sz = lua_rawlen(L, 1);
	lua_pushinteger(L, sz);
	return 1;
}

static void
make_object(lua_State *L, int type, const void * ptr, size_t len) {
	luaL_Buffer b;
	luaL_buffinit(L, &b);
	luaL_addchar(&b, 0);
	luaL_addchar(&b, type);
	luaL_addlstring(&b, ptr, len);
	luaL_pushresult(&b);
}

static void
unpack_dict(lua_State *L, struct bson_reader *br, bool array) {
	luaL_checkstack(L, 16, NULL);	// reserve enough stack space to unpack table
	int sz = read_int32(L, br);
	const void * bytes = read_bytes(L, br, sz-5);
	struct bson_reader t = { bytes, sz-5 };
	int end = read_byte(L, br);
	if (end != '\0') {
		luaL_error(L, "Invalid document end");
	}

	lua_newtable(L);

	for (;;) {
		if (t.size == 0)
			break;
		int bt = read_byte(L, &t);
		size_t klen = 0;
		const char * key = read_cstring(L, &t, &klen);
		if (array) {
			int id = strtol(key, NULL, 10) + 1;
			lua_pushinteger(L,id);
		} else {
			lua_pushlstring(L, key, klen);
		}
		switch (bt) {
		case BSON_REAL:
			lua_pushnumber(L, read_double(L, &t));
			break;
		case BSON_BOOLEAN:
			lua_pushboolean(L, read_byte(L, &t));
			break;
		case BSON_STRING: {
			int sz = read_int32(L, &t);
			if (sz <= 0) {
				luaL_error(L, "Invalid bson string , length = %d", sz);
			}
			lua_pushlstring(L, read_bytes(L, &t, sz), sz-1);
			break;
		}
		case BSON_DOCUMENT:
			unpack_dict(L, &t, false);
			break;
		case BSON_ARRAY:
			unpack_dict(L, &t, true);
			break;
		case BSON_BINARY: {
			int sz = read_int32(L, &t);
			int subtype = read_byte(L, &t);

			luaL_Buffer b;
			luaL_buffinit(L, &b);
			luaL_addchar(&b, 0);
			luaL_addchar(&b, BSON_BINARY);
			luaL_addchar(&b, subtype);
			luaL_addlstring(&b, read_bytes(L, &t, sz), sz);
			luaL_pushresult(&b);
			break;
		}
		case BSON_OBJECTID:
			make_object(L, BSON_OBJECTID, read_bytes(L, &t, 12), 12);
			break;
		case BSON_DATE: {
			int64_t date = read_int64(L, &t);
			uint32_t v = date / 1000;
			make_object(L, BSON_DATE, &v, 4);
			break;
		}
		case BSON_MINKEY:
		case BSON_MAXKEY:
		case BSON_NULL: {
			char key[] = { 0, bt };
			lua_pushlstring(L, key, sizeof(key));
			break;
		}
		case BSON_REGEX: {
			size_t rlen1=0;
			size_t rlen2=0;
			const char * r1 = read_cstring(L, &t, &rlen1);
			const char * r2 = read_cstring(L, &t, &rlen2);
			luaL_Buffer b;
			luaL_buffinit(L, &b);
			luaL_addchar(&b, 0);
			luaL_addchar(&b, BSON_REGEX);
			luaL_addlstring(&b, r1, rlen1);
			luaL_addchar(&b,0);
			luaL_addlstring(&b, r2, rlen2);
			luaL_addchar(&b,0);
			luaL_pushresult(&b);
			break;
		}
		case BSON_INT32:
			lua_pushinteger(L, read_int32(L, &t));
			break;
		case BSON_TIMESTAMP: {
			int32_t inc = read_int32(L, &t);
			int32_t ts = read_int32(L, &t);

			luaL_Buffer b;
			luaL_buffinit(L, &b);
			luaL_addchar(&b, 0);
			luaL_addchar(&b, BSON_TIMESTAMP);
			luaL_addlstring(&b, (const char *)&inc, 4);
			luaL_addlstring(&b, (const char *)&ts, 4);
			luaL_pushresult(&b);
			break;
		}
		case BSON_INT64:
			lua_pushinteger(L, read_int64(L, &t));
			break;
		case BSON_DBPOINTER: {
			const void * ptr = t.ptr;
			int sz = read_int32(L, &t);
			read_bytes(L, &t, sz+12);
			make_object(L, BSON_DBPOINTER, ptr, sz + 16);
			break;
		}
		case BSON_JSCODE:
		case BSON_SYMBOL: {
			const void * ptr = t.ptr;
			int sz = read_int32(L, &t);
			read_bytes(L, &t, sz);
			make_object(L, bt, ptr, sz + 4);
			break;
		}
		case BSON_CODEWS: {
			const void * ptr = t.ptr;
			int sz = read_int32(L, &t);
			read_bytes(L, &t, sz-4);
			make_object(L, bt, ptr, sz);
			break;
		}
		default:
			// unsupported
			luaL_error(L, "Invalid bson type : %d", bt);
			lua_pop(L,1);
			continue;
		}
		lua_rawset(L,-3);
	}
}

static int
lmakeindex(lua_State *L) {
	int32_t *bson = luaL_checkudata(L,1,"bson");
	const uint8_t * start = (const uint8_t *)bson;
	struct bson_reader br = { start+4, get_length(start) - 5 };
	lua_newtable(L);

	for (;;) {
		if (br.size == 0)
			break;
		int bt = read_byte(L, &br);
		size_t klen = 0;
		const char * key = read_cstring(L, &br, &klen);
		int field_size = 0;
		switch (bt) {
		case BSON_INT64:
		case BSON_TIMESTAMP: 
		case BSON_DATE:
		case BSON_REAL:
			field_size = 8;
			break;
		case BSON_BOOLEAN:
			field_size = 1;
			break;
		case BSON_JSCODE:
		case BSON_SYMBOL: 
		case BSON_STRING: {
			int sz = read_int32(L, &br);
			read_bytes(L, &br, sz);
			break;
		}
		case BSON_CODEWS:
		case BSON_ARRAY:
		case BSON_DOCUMENT: {
			int sz = read_int32(L, &br);
			read_bytes(L, &br, sz-4);
			break;
		}
		case BSON_BINARY: {
			int sz = read_int32(L, &br);
			read_bytes(L, &br, sz+1);
			break;
		}
		case BSON_OBJECTID:
			field_size = 12;
			break;
		case BSON_MINKEY:
		case BSON_MAXKEY:
		case BSON_NULL:
			break;
		case BSON_REGEX: {
			size_t rlen1=0;
			size_t rlen2=0;
			read_cstring(L, &br, &rlen1);
			read_cstring(L, &br, &rlen2);
			break;
		}
		case BSON_INT32:
			field_size = 4;
			break;
		case BSON_DBPOINTER: {
			int sz = read_int32(L, &br);
			read_bytes(L, &br, sz+12);
			break;
		}
		default:
			// unsupported
			luaL_error(L, "Invalid bson type : %d", bt);
			lua_pop(L,1);
			continue;
		}
		if (field_size > 0) {
			int id = bt | (int)(br.ptr - start) << BSON_TYPE_SHIFT;
			read_bytes(L, &br, field_size);
			lua_pushlstring(L, key, klen);
			lua_pushinteger(L,id);
			lua_rawset(L,-3);
		}
	}
	lua_setuservalue(L,1);
	lua_settop(L,1);

	return 1;
}

static void
replace_object(lua_State *L, int type, struct bson * bs) {
	size_t len = 0;
	const char * data = luaL_checklstring(L,3, &len);
	if (len < 6 || data[0] != 0 || data[1] != type) {
		luaL_error(L, "Type mismatch, need bson type %d", type);
	}
	switch (type) {
	case BSON_OBJECTID:
		if (len != 2+12) {
			luaL_error(L, "Invalid object id");
		}
		memcpy(bs->ptr, data+2, 12);
		break;
	case BSON_DATE: {
		if (len != 2+4) {
			luaL_error(L, "Invalid date");
		}
		const uint32_t * ts = (const uint32_t *)(data + 2);
		int64_t v = (int64_t)*ts * 1000;
		write_int64(bs, v);
		break;
	}
	case BSON_TIMESTAMP: {
		if (len != 2+8) {
			luaL_error(L, "Invalid timestamp");
		}
		const uint32_t * inc = (const uint32_t *)(data + 2);
		const uint32_t * ts = (const uint32_t *)(data + 6);
		write_int32(bs, *inc);
		write_int32(bs, *ts);
		break;
	}
	}
}

static int
lreplace(lua_State *L) {
	lua_getuservalue(L,1);
	if (!lua_istable(L,-1)) {
		return luaL_error(L, "call makeindex first");
	}
	lua_pushvalue(L,2);
	if (lua_rawget(L, -2) != LUA_TNUMBER) {
		return luaL_error(L, "Can't replace key : %s", lua_tostring(L,2));
	}
	int id = lua_tointeger(L, -1);
	int type = id & ((1<<(BSON_TYPE_SHIFT)) - 1);
	int offset = id >> BSON_TYPE_SHIFT;
	uint8_t * start = lua_touserdata(L,1);
	struct bson b = { 0,16, start + offset };
	switch (type) {
	case BSON_REAL:
		write_double(&b, luaL_checknumber(L, 3));
		break;
	case BSON_BOOLEAN:
		write_byte(&b, lua_toboolean(L,3));
		break;
	case BSON_OBJECTID:
	case BSON_DATE:
	case BSON_TIMESTAMP:
		replace_object(L, type, &b);
		break;
	case BSON_INT32: {
		if (!lua_isinteger(L, 3)) {
			luaL_error(L, "%f must be a 32bit integer ", lua_tonumber(L, 3));
		}
		int32_t i = lua_tointeger(L,3);
		write_int32(&b, i);
		break;
	}
	case BSON_INT64: {
		if (!lua_isinteger(L, 3)) {
			luaL_error(L, "%f must be a 64bit integer ", lua_tonumber(L, 3));
		}
		int64_t i = lua_tointeger(L,3);
		write_int64(&b, i);
		break;
	}
	default:
		luaL_error(L, "Can't replace type %d", type);
		break;
	}
	return 0;
}

static int
ldecode(lua_State *L) {
	const int32_t * data = lua_touserdata(L,1);
	if (data == NULL) {
		return 0;
	}
	const uint8_t * b = (const uint8_t *)data;
	int32_t len = get_length(b);
	struct bson_reader br = { b , len };

	unpack_dict(L, &br, false);

	return 1;
}

static void
bson_meta(lua_State *L) {
	if (luaL_newmetatable(L, "bson")) {
		luaL_Reg l[] = {
			{ "decode", ldecode },
			{ "makeindex", lmakeindex },
			{ NULL, NULL },
		};
		luaL_newlib(L,l);
		lua_setfield(L, -2, "__index");
		lua_pushcfunction(L, ltostring);
		lua_setfield(L, -2, "__tostring");
		lua_pushcfunction(L, llen);
		lua_setfield(L, -2, "__len");
		lua_pushcfunction(L, lreplace);
		lua_setfield(L, -2, "__newindex");
	}
	lua_setmetatable(L, -2);
}

static int
lencode(lua_State *L) {
	struct bson b;
	bson_create(&b);
	lua_settop(L,1);
	luaL_checktype(L, 1, LUA_TTABLE);
	pack_dict(L, &b, false, 0);
	void * ud = lua_newuserdata(L, b.size);
	memcpy(ud, b.ptr, b.size);
	bson_destroy(&b);
	bson_meta(L);
	return 1;
}

static int
lencode_order(lua_State *L) {
	struct bson b;
	bson_create(&b);
	int n = lua_gettop(L);
	if (n%2 != 0) {
		return luaL_error(L, "Invalid ordered dict");
	}
	pack_ordered_dict(L, &b, n, 0);
	lua_settop(L,1);
	void * ud = lua_newuserdata(L, b.size);
	memcpy(ud, b.ptr, b.size);
	bson_destroy(&b);
	bson_meta(L);
	return 1;
}

static int
ldate(lua_State *L) {
	int d = luaL_checkinteger(L,1);
	luaL_Buffer b;
	luaL_buffinit(L, &b);
	luaL_addchar(&b, 0);
	luaL_addchar(&b, BSON_DATE);
	luaL_addlstring(&b, (const char *)&d, sizeof(d));
	luaL_pushresult(&b);

	return 1;
}

static int
ltimestamp(lua_State *L) {
	int d = luaL_checkinteger(L,1);
	luaL_Buffer b;
	luaL_buffinit(L, &b);
	luaL_addchar(&b, 0);
	luaL_addchar(&b, BSON_TIMESTAMP);
	if (lua_isnoneornil(L,2)) {
		static uint32_t inc = 0;
		luaL_addlstring(&b, (const char *)&inc, sizeof(inc));
		++inc;
	} else {
		uint32_t i = (uint32_t)lua_tointeger(L,2);
		luaL_addlstring(&b, (const char *)&i, sizeof(i));
	}
	luaL_addlstring(&b, (const char *)&d, sizeof(d));
	luaL_pushresult(&b);

	return 1;
}

static int
lregex(lua_State *L) {
	luaL_checkstring(L,1);
	if (lua_gettop(L) < 2) {
		lua_pushliteral(L,"");
	}
	luaL_Buffer b;
	luaL_buffinit(L, &b);
	luaL_addchar(&b, 0);
	luaL_addchar(&b, BSON_REGEX);
	lua_pushvalue(L,1);
	luaL_addvalue(&b);
	luaL_addchar(&b,0);
	lua_pushvalue(L,2);
	luaL_addvalue(&b);
	luaL_addchar(&b,0);
	luaL_pushresult(&b);

	return 1;
}

static int
lbinary(lua_State *L) {
	lua_settop(L,1);
	luaL_Buffer b;
	luaL_buffinit(L, &b);
	luaL_addchar(&b, 0);
	luaL_addchar(&b, BSON_BINARY);
	luaL_addchar(&b, 0);	// sub type
	luaL_addvalue(&b);
	luaL_pushresult(&b);

	return 1;
}

static int
lsubtype(lua_State *L, int subtype, const uint8_t * buf, size_t sz) {
	switch(subtype) {
	case BSON_BINARY:
		lua_pushvalue(L, lua_upvalueindex(6));
		lua_pushlstring(L, (const char *)buf+1, sz-1);
		lua_pushinteger(L, buf[0]);
		return 3;
	case BSON_OBJECTID: {
		if (sz != 12) {
			return luaL_error(L, "Invalid object id");
		}
		char oid[24];
		int i;
		const uint8_t * id = buf;
		static char *hex = "0123456789abcdef";
		for (i=0;i<12;i++) {
			oid[i*2] = hex[id[i] >> 4];
			oid[i*2+1] = hex[id[i] & 0xf];
		}
		lua_pushvalue(L, lua_upvalueindex(7));
		lua_pushlstring(L, oid, 24);

		return 2;
	}
	case BSON_DATE: {
		if (sz != 4) {
			return luaL_error(L, "Invalid date");
		}
		int d = *(const int *)buf;
		lua_pushvalue(L, lua_upvalueindex(9));
		lua_pushinteger(L, d);
		return 2;
	}
	case BSON_TIMESTAMP: {
		if (sz != 8) {
			return luaL_error(L, "Invalid timestamp");
		}
		const uint32_t * ts = (const uint32_t *)buf;
		lua_pushvalue(L, lua_upvalueindex(8));
		lua_pushinteger(L, (lua_Integer)ts[1]);
		lua_pushinteger(L, (lua_Integer)ts[0]);
		return 3;
	}
	case BSON_REGEX: {
		--sz;
		size_t i;
		const uint8_t *str = buf;
		for (i=0;i<sz;i++) {
			if (str[sz-i-1]==0) {
				break;
			}
		}
		lua_pushvalue(L, lua_upvalueindex(10));
		if (i==sz) {
			return luaL_error(L, "Invalid regex");
		}
		lua_pushlstring(L, (const char *)str, sz - i - 1);
		lua_pushlstring(L, (const char *)str+sz-i, i);
		return 3;
	}
	case BSON_MINKEY:
		lua_pushvalue(L, lua_upvalueindex(11));
		return 1;
	case BSON_MAXKEY:
		lua_pushvalue(L, lua_upvalueindex(12));
		return 1;
	case BSON_NULL:
		lua_pushvalue(L, lua_upvalueindex(4));
		return 1;
	case BSON_JSCODE:
	case BSON_DBPOINTER:
	case BSON_SYMBOL:
	case BSON_CODEWS:
		lua_pushvalue(L, lua_upvalueindex(13));
		lua_pushlstring(L, (const char *)buf, sz);
		return 2;
	default:
		return luaL_error(L, "Invalid subtype %d", subtype);
	}
}

static int
ltype(lua_State *L) {
	int t = lua_type(L,1);
	int type = 0;
	switch (t) {
	case LUA_TNUMBER:
		type = 1;
		break;
	case LUA_TBOOLEAN:
		type = 2;
		break;
	case LUA_TTABLE:
		type = 3;
		break;
	case LUA_TNIL:
		lua_pushvalue(L, lua_upvalueindex(4));
		return 1;
	case LUA_TSTRING: {
		size_t len = 0;
		const char * str = lua_tolstring(L,1,&len);
		if (str[0] == 0 && len >= 2) {
			return lsubtype(L, (uint8_t)str[1], (const uint8_t *)str+2, len-2);
		} else {
			type = 5;
			break;
		}
	}
	default:
		return luaL_error(L, "Invalid type %s",lua_typename(L,t));
	}
	lua_pushvalue(L, lua_upvalueindex(type));
	lua_pushvalue(L,1);
	return 2;
}

static void
typeclosure(lua_State *L) {
	static const char * typename[] = {
		"number",	// 1
		"boolean",	// 2
		"table",	// 3
		"nil",		// 4
		"string",	// 5
		"binary",	// 6
		"objectid",	// 7
		"timestamp",    // 8
		"date",		// 9
		"regex",	// 10
		"minkey",	// 11
		"maxkey",	// 12
		"unsupported", // 13
	};
	int i;
	int n = sizeof(typename)/sizeof(typename[0]);
	for (i=0;i<n;i++) {
		lua_pushstring(L,typename[i]);
	}
	lua_pushcclosure(L, ltype, n);
}

static uint8_t oid_header[5];
static uint32_t oid_counter;

static void
init_oid_header() {
	if (oid_counter) {
		// already init
		return;
	}
	pid_t pid = getpid();
	uint32_t h = 0;
	char hostname[256];
	if (gethostname(hostname, sizeof(hostname))==0) {
		int i;
		for (i=0;i<sizeof(hostname) && hostname[i];i++) {
			h = h ^ ((h<<5)+(h>>2)+hostname[i]);
 		}
		h ^= i;
	}
	oid_header[0] = h & 0xff;
	oid_header[1] = (h>>8) & 0xff;
	oid_header[2] = (h>>16) & 0xff;
	oid_header[3] = pid & 0xff;
	oid_header[4] = (pid >> 8) & 0xff;
	
	uint32_t c = h ^ time(NULL) ^ (uintptr_t)&h;
	if (c == 0) {
		c = 1;
	}
	oid_counter = c;
}

static inline int
hextoint(char c) {
	if (c>='0' && c<='9')
		return c-'0';
	if (c>='a' && c<='z')
		return c-'a'+10;
	if (c>='A' && c<='Z')
		return c-'A'+10;
	return 0;
}

static int
lobjectid(lua_State *L) {
	uint8_t oid[14] = { 0, BSON_OBJECTID };
	if (lua_isstring(L,1)) {
		size_t len;
		const char * str = lua_tolstring(L,1,&len);
		if (len != 24) {
			return luaL_error(L, "Invalid objectid %s", str);
		}
		int i;
		for (i=0;i<12;i++) {
			oid[i+2] = hextoint(str[i*2]) << 4 | hextoint(str[i*2+1]);
		}
	} else {
		time_t ti = time(NULL);
		// old_counter is a static var, use atom inc.
		uint32_t id = ATOM_FINC(&oid_counter);

		oid[2] = (ti>>24) & 0xff;
		oid[3] = (ti>>16) & 0xff;
		oid[4] = (ti>>8) & 0xff;
		oid[5] = ti & 0xff;
		memcpy(oid+6 , oid_header, 5);
		oid[11] = (id>>16) & 0xff; 
		oid[12] = (id>>8) & 0xff; 
		oid[13] = id & 0xff;
	}
	lua_pushlstring( L, (const char *)oid, 14);

	return 1;
}

int
luaopen_bson(lua_State *L) {
	luaL_checkversion(L);
	int i;
	for (i=0;i<MAX_NUMBER;i++) {
		char tmp[8];
		bson_numstr_len[i] = sprintf(tmp,"%d",i);
		memcpy(bson_numstrs[i], tmp, bson_numstr_len[i]);
	}
	luaL_Reg l[] = {
		{ "encode", lencode },
		{ "encode_order", lencode_order },
		{ "date", ldate },
		{ "timestamp", ltimestamp  },
		{ "regex", lregex },
		{ "binary", lbinary },
		{ "objectid", lobjectid },
		{ "decode", ldecode },
		{ NULL,  NULL },
	};

	luaL_newlib(L,l);

	typeclosure(L);
	lua_setfield(L,-2,"type");
	char null[] = { 0, BSON_NULL };
	lua_pushlstring(L, null, sizeof(null));
	lua_setfield(L,-2,"null");
	char minkey[] = { 0, BSON_MINKEY };
	lua_pushlstring(L, minkey, sizeof(minkey));
	lua_setfield(L,-2,"minkey");
	char maxkey[] = { 0, BSON_MAXKEY };
	lua_pushlstring(L, maxkey, sizeof(maxkey));
	lua_setfield(L,-2,"maxkey");
	init_oid_header();

	return 1;
}

