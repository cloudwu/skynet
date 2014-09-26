#include <string.h>
#include "msvcint.h"

#include "lua.h"
#include "lauxlib.h"
#include "sproto.h"

#define ENCODE_BUFFERSIZE 2050

//#define ENCODE_BUFFERSIZE 2050
#define ENCODE_MAXSIZE 0x1000000
#define ENCODE_DEEPLEVEL 64

#ifndef luaL_newlib /* using LuaJIT */
/*
** set functions from list 'l' into table at top - 'nup'; each
** function gets the 'nup' elements at the top as upvalues.
** Returns with only the table at the stack.
*/
LUALIB_API void luaL_setfuncs (lua_State *L, const luaL_Reg *l, int nup) {
#ifdef luaL_checkversion
  luaL_checkversion(L);
#endif
  luaL_checkstack(L, nup, "too many upvalues");
  for (; l->name != NULL; l++) {  /* fill the table with given functions */
    int i;
    for (i = 0; i < nup; i++)  /* copy upvalues to the top */
      lua_pushvalue(L, -nup);
    lua_pushcclosure(L, l->func, nup);  /* closure with those upvalues */
    lua_setfield(L, -(nup + 2), l->name);
  }
  lua_pop(L, nup);  /* remove upvalues */
}

#define luaL_newlibtable(L,l) \
  lua_createtable(L, 0, sizeof(l)/sizeof((l)[0]) - 1)

#define luaL_newlib(L,l)  (luaL_newlibtable(L,l), luaL_setfuncs(L,l,0))
#endif

static int
lnewproto(lua_State *L) {
	size_t sz = 0;
	void * buffer = (void *)luaL_checklstring(L,1,&sz);
	struct sproto * sp = sproto_create(buffer, sz);
	if (sp) {
		lua_pushlightuserdata(L, sp);
		return 1;
	}
	return 0;
}

static int
ldeleteproto(lua_State *L) {
	struct sproto * sp = lua_touserdata(L,1);
	if (sp == NULL) {
		return luaL_argerror(L, 1, "Need a sproto object");
	}
	sproto_release(sp);
	return 0;
}

static int
lquerytype(lua_State *L) {
	struct sproto *sp = lua_touserdata(L,1);
	if (sp == NULL) {
		return luaL_argerror(L, 1, "Need a sproto object");
	}
	const char * typename = luaL_checkstring(L,2);
	struct sproto_type *st = sproto_type(sp, typename);
	if (st) {
		lua_pushlightuserdata(L, st);
		return 1;
	}

	return luaL_error(L, "type %s not found", typename);
}

struct encode_ud {
	lua_State *L;
	struct sproto_type *st;
	int tbl_index;
	const char * array_tag;
	int array_index;
	int deep;
};

static int 
encode(void *ud, const char *tagname, int type, int index, struct sproto_type *st, void *value, int length) {
	struct encode_ud *self = ud;
	lua_State *L = self->L;
	if (self->deep >= ENCODE_DEEPLEVEL)
		return luaL_error(L, "The table is too deep");
	if (index > 0) {
		if (tagname != self->array_tag) {
			self->array_tag = tagname;
			lua_getfield(L, self->tbl_index, tagname);
			if (lua_isnil(L, -1)) {
				if (self->array_index) {
					lua_replace(L, self->array_index);
				}
				self->array_index = 0;
				return 0;
			}
			if (self->array_index) {
				lua_replace(L, self->array_index);
			} else {
				self->array_index = lua_gettop(L);
			}
		}
		lua_rawgeti(L, self->array_index, index);
	} else {
		lua_getfield(L, self->tbl_index, tagname);
	}
	if (lua_isnil(L, -1)) {
		lua_pop(L,1);
		return 0;
	}
	switch (type) {
	case SPROTO_TINTEGER: {
		lua_Integer v = luaL_checkinteger(L, -1);
		lua_pop(L,1);
		// notice: in lua 5.2, lua_Integer maybe 52bit
		lua_Integer vh = v >> 31;
		if (vh == 0 || vh == -1) {
			*(uint32_t *)value = (uint32_t)v;
			return 4;
		}
		else {
			*(uint64_t *)value = (uint64_t)v;
			return 8;
		}
	}
	case SPROTO_TBOOLEAN: {
		int v = lua_toboolean(L, -1);
		*(int *)value = v;
		lua_pop(L,1);
		return 4;
	}
	case SPROTO_TSTRING: {
		size_t sz = 0;
		const char * str = luaL_checklstring(L, -1, &sz);
		if (sz > length)
			return -1;
		memcpy(value, str, sz);
		lua_pop(L,1);
		return sz;
	}
	case SPROTO_TSTRUCT: {
		struct encode_ud sub;
		sub.L = L;
		sub.st = st;
		sub.tbl_index = lua_gettop(L);
		sub.array_tag = NULL;
		sub.array_index = 0;
		sub.deep = self->deep + 1;
		int r = sproto_encode(st, value, length, encode, &sub);
		lua_pop(L,1);
		return r;
	}
	default:
		return luaL_error(L, "Invalid field type %d", type);
	}
}

static void *
expand_buffer(lua_State *L, int osz, int nsz) {
	do {
		osz *= 2;
	} while (osz < nsz);
	if (osz > ENCODE_MAXSIZE) {
		luaL_error(L, "object is too large (>%d)", ENCODE_MAXSIZE);
		return NULL;
	}
	void *output = lua_newuserdata(L, osz);
	lua_replace(L, lua_upvalueindex(1));
	lua_pushinteger(L, osz);
	lua_replace(L, lua_upvalueindex(2));

	return output;
}

/*
	lightuserdata sproto_type
	table source

	return string 
 */
static int
lencode(lua_State *L) {
	void * buffer = lua_touserdata(L, lua_upvalueindex(1));
	int sz = lua_tointeger(L, lua_upvalueindex(2));

	struct sproto_type * st = lua_touserdata(L, 1);
	if (st == NULL) {
		return luaL_argerror(L, 1, "Need a sproto_type object");
	}
	luaL_checktype(L, 2, LUA_TTABLE);
	luaL_checkstack(L, ENCODE_DEEPLEVEL + 8, NULL);
	struct encode_ud self;
	self.L = L;
	self.st = st;
	self.tbl_index = 2;
	self.array_tag = NULL;
	self.array_index = 0;
	self.deep = 0;
	for (;;) {
		int r = sproto_encode(st, buffer, sz, encode, &self);
		if (r<0) {
			buffer = expand_buffer(L, sz, sz*2);
			sz *= 2;
		} else {
			lua_pushlstring(L, buffer, r);
			return 1;
		}
	}
}

struct decode_ud {
	lua_State *L;
	const char * array_tag;
	int array_index;
	int result_index;
	int deep;
};

static int 
decode(void *ud, const char *tagname, int type, int index, struct sproto_type *st, void *value, int length) {
	struct decode_ud * self = ud;
	lua_State *L = self->L;
	if (self->deep >= ENCODE_DEEPLEVEL)
		return luaL_error(L, "The table is too deep");
	if (index > 0) {
		// It's array
		if (tagname != self->array_tag) {
			self->array_tag = tagname;
			lua_newtable(L);
			lua_pushvalue(L, -1);
			lua_setfield(L, self->result_index, tagname);
			if (self->array_index) {
				lua_replace(L, self->array_index);
			} else {
				self->array_index = lua_gettop(L);
			}
		}
	}
	switch (type) {
	case SPROTO_TINTEGER: {
		// notice: in lua 5.2, 52bit integer support (not 64)
		lua_Integer v = *(lua_Integer *)value;
		lua_pushinteger(L, v);
		break;
	}
	case SPROTO_TBOOLEAN: {
		int v = *(lua_Integer*)value;
		lua_pushboolean(L,v);
		break;
	}
	case SPROTO_TSTRING: {
		lua_pushlstring(L, value, length);
		break;
	}
	case SPROTO_TSTRUCT: {
		lua_newtable(L);
		struct decode_ud sub;
		sub.L = L;
		sub.result_index = lua_gettop(L);
		sub.deep = self->deep + 1;
		sub.array_index = 0;
		sub.array_tag = NULL;

		int r = sproto_decode(st, value, length, decode, &sub);
		if (r < 0 || r != length)
			return r;
		lua_settop(L, sub.result_index);
		break;
	}
	default:
		luaL_error(L, "Invalid type");
	}
	if (index > 0) {
		lua_rawseti(L, self->array_index, index);
	} else {
		lua_setfield(L, self->result_index, tagname);
	}

	return 0;
}

static const void *
getbuffer(lua_State *L, int index, size_t *sz) {
	const void * buffer = NULL;
	int t = lua_type(L, index);
	if (t == LUA_TSTRING) {
		buffer = lua_tolstring(L, index, sz);
	} else {
		if (t != LUA_TUSERDATA && t != LUA_TLIGHTUSERDATA) {
			luaL_argerror(L, index, "Need a string or userdata");
			return NULL;
		}
		buffer = lua_touserdata(L, index);
		*sz = luaL_checkinteger(L, index+1);
	}
	return buffer;
}

/*
	lightuserdata sproto_type
	string source	/  (lightuserdata , integer)
	return table
 */
static int
ldecode(lua_State *L) {
	struct sproto_type * st = lua_touserdata(L, 1);
	if (st == NULL) {
		return luaL_argerror(L, 1, "Need a sproto_type object");
	}
	size_t sz=0;
	const void * buffer = getbuffer(L, 2, &sz);
	if (!lua_istable(L, -1)) {
		lua_newtable(L);
	}
	luaL_checkstack(L, ENCODE_DEEPLEVEL*2 + 8, NULL);
	struct decode_ud self;
	self.L = L;
	self.result_index = lua_gettop(L);
	self.array_index = 0;
	self.array_tag = NULL;
	self.deep = 0;
	int r = sproto_decode(st, buffer, (int)sz, decode, &self);
	if (r < 0) {
		return luaL_error(L, "decode error");
	}
	lua_settop(L, self.result_index);
	lua_pushinteger(L, r);
	return 2;
}

static int
ldumpproto(lua_State *L) {
	struct sproto * sp = lua_touserdata(L, 1);
	if (sp == NULL) {
		return luaL_argerror(L, 1, "Need a sproto_type object");
	}
	sproto_dump(sp);

	return 0;
}


/*
	string source	/  (lightuserdata , integer)
	return string
 */
static int
lpack(lua_State *L) {
	size_t sz=0;
	const void * buffer = getbuffer(L, 1, &sz);
	// the worst-case space overhead of packing is 2 bytes per 2 KiB of input (256 words = 2KiB).
	size_t maxsz = (sz + 2047) / 2048 * 2 + sz;
	void * output = lua_touserdata(L, lua_upvalueindex(1));
	int osz = lua_tointeger(L, lua_upvalueindex(2));
	if (osz < maxsz) {
		output = expand_buffer(L, osz, maxsz);
	}
	int bytes = sproto_pack(buffer, sz, output, maxsz);
	if (bytes > maxsz) {
		return luaL_error(L, "packing error, return size = %d", bytes);
	}
	lua_pushlstring(L, output, bytes);

	return 1;
}

static int
lunpack(lua_State *L) {
	size_t sz=0;
	const void * buffer = getbuffer(L, 1, &sz);
	void * output = lua_touserdata(L, lua_upvalueindex(1));
	int osz = lua_tointeger(L, lua_upvalueindex(2));
	int r = sproto_unpack(buffer, sz, output, osz);
	if (r < 0)
		return luaL_error(L, "Invalid unpack stream");
	if (r > osz) {
		output = expand_buffer(L, osz, r);
	}
	r = sproto_unpack(buffer, sz, output, r);
	if (r < 0)
		return luaL_error(L, "Invalid unpack stream");
	lua_pushlstring(L, output, r);
	return 1;
}

static void
pushfunction_withbuffer(lua_State *L, const char * name, lua_CFunction func) {
	lua_newuserdata(L, ENCODE_BUFFERSIZE);
	lua_pushinteger(L, ENCODE_BUFFERSIZE);
	lua_pushcclosure(L, func, 2);
	lua_setfield(L, -2, name);
}

static int
lprotocol(lua_State *L) {
	struct sproto * sp = lua_touserdata(L, 1);
	if (sp == NULL) {
		return luaL_argerror(L, 1, "Need a sproto_type object");
	}
	int t = lua_type(L,2);
	int tag;
	if (t == LUA_TNUMBER) {
		tag = lua_tointeger(L, 2);
		const char * name = sproto_protoname(sp, tag);
		if (name == NULL)
			return 0;
		lua_pushstring(L, name);
	} else {
		const char * name = lua_tostring(L, 2);
		tag = sproto_prototag(sp, name);
		if (tag < 0)
			return 0;
		lua_pushinteger(L, tag);
	}
	struct sproto_type * request = sproto_protoquery(sp, tag, SPROTO_REQUEST);
	if (request == NULL) {
		lua_pushnil(L);
	} else {
		lua_pushlightuserdata(L, request);
	}
	struct sproto_type * response = sproto_protoquery(sp, tag, SPROTO_RESPONSE);
	if (response == NULL) {
		lua_pushnil(L);
	} else {
		lua_pushlightuserdata(L, response);
	}
	return 3;
}

int
luaopen_sproto_core(lua_State *L) {
#ifdef luaL_checkversion
	luaL_checkversion(L);
#endif
	luaL_Reg l[] = {
		{ "newproto", lnewproto },
		{ "deleteproto", ldeleteproto },
		{ "dumpproto", ldumpproto },
		{ "querytype", lquerytype },
		{ "decode", ldecode },
		{ "protocol", lprotocol },
		{ NULL, NULL },
	};
	luaL_newlib(L,l);
	pushfunction_withbuffer(L, "encode", lencode);
	pushfunction_withbuffer(L, "pack", lpack);
	pushfunction_withbuffer(L, "unpack", lunpack);
	return 1;
}
