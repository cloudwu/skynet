#define LUA_LIB

#include <string.h>
#include <stdlib.h>
#include <math.h>
#include "msvcint.h"

#include "lua.h"
#include "lauxlib.h"
#include "sproto.h"

#define MAX_GLOBALSPROTO 16
#define ENCODE_BUFFERSIZE 2050

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

#if LUA_VERSION_NUM < 503

#if LUA_VERSION_NUM < 502
static int64_t lua_tointegerx(lua_State *L, int idx, int *isnum) {
	if (lua_isnumber(L, idx)) {
		if (isnum) *isnum = 1;
		return (int64_t)lua_tointeger(L, idx);
	}
	else {
		if (isnum) *isnum = 0;
		return 0;
	}
}

static int lua_absindex (lua_State *L, int idx) {
	if (idx > 0 || idx <= LUA_REGISTRYINDEX)
		return idx;
	return lua_gettop(L) + idx + 1;
}

#endif

static void
lua_geti(lua_State *L, int index, lua_Integer i) {
	index = lua_absindex(L, index);
	lua_pushinteger(L, i);
	lua_gettable(L, index);
}

static void
lua_seti(lua_State *L, int index, lua_Integer n) {
	index = lua_absindex(L, index);
	lua_pushinteger(L, n);
	lua_insert(L, -2);
	lua_settable(L, index);
}

#endif

#if defined(SPROTO_WEAK_TYPE)
static int64_t
tointegerx (lua_State *L, int idx, int *isnum) {
	int _isnum = 0;
	int64_t v = lua_tointegerx(L, idx, &_isnum);
	if (!_isnum){
		double num = lua_tonumberx(L, idx, &_isnum);
		if(_isnum) {
			v = (int64_t)llround(num);
		}
	}
	if(isnum) *isnum = _isnum;
	return v;
}

static int
tobooleanx (lua_State *L, int idx, int *isbool) {
	if (isbool) *isbool = 1;
	return lua_toboolean(L, idx);
}

static const char *
tolstringx (lua_State *L, int idx, size_t *len, int *isstring) {
	const char * str = luaL_tolstring(L, idx, len); // call metamethod, '__tostring' must return a string
	if (isstring) {
		*isstring = 1;
	}
	lua_pop(L, 1);
	return str;
}

#else
#define tointegerx(L, idx, isnum) lua_tointegerx((L), (idx), (isnum))

static int
tobooleanx (lua_State *L, int idx, int *isbool) {
	if (isbool) *isbool = lua_isboolean(L, idx);
	return lua_toboolean(L, idx);
}

static const char *
tolstringx (lua_State *L, int idx, size_t *len, int *isstring) {
	if (isstring) {
		*isstring = (lua_type(L, idx) == LUA_TSTRING);
	}
	const char * str = lua_tolstring(L, idx, len);
	return str;
}

#endif

static int
lnewproto(lua_State *L) {
	struct sproto * sp;
	size_t sz;
	void * buffer = (void *)luaL_checklstring(L,1,&sz);
	sp = sproto_create(buffer, sz);
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
	const char * type_name;
	struct sproto *sp = lua_touserdata(L,1);
	struct sproto_type *st;
	if (sp == NULL) {
		return luaL_argerror(L, 1, "Need a sproto object");
	}
	type_name = luaL_checkstring(L,2);
	st = sproto_type(sp, type_name);
	if (st) {
		lua_pushlightuserdata(L, st);
		return 1;
	}
	return 0;
}

struct encode_ud {
	lua_State *L;
	struct sproto_type *st;
	int tbl_index;
	const char * array_tag;
	int array_index;
	int deep;
	int map_entry;
	int iter_func;
	int iter_table;
	int iter_key;
};

static int
next_list(lua_State *L, struct encode_ud * self) {
	// todo: check the key is equal to mainindex value
	if (self->iter_func) {
		lua_pushvalue(L, self->iter_func);
		lua_pushvalue(L, self->iter_table);
		lua_pushvalue(L, self->iter_key);
		lua_call(L, 2, 2);
		if (lua_isnil(L, -2)) {
			lua_pop(L, 2);
			return 0;
		}
		return 1;
	} else {
		lua_pushvalue(L,self->iter_key);
		return lua_next(L, self->array_index);
	}
}

static int
get_encodefield(const struct sproto_arg *args) {
	struct encode_ud *self = args->ud;
	lua_State *L = self->L;
	if (args->index > 0) {
		int map = args->ktagname != NULL;
		if (args->tagname != self->array_tag) {
			// a new array
			self->array_tag = args->tagname;
			lua_getfield(L, self->tbl_index, args->tagname);
			if (lua_isnil(L, -1)) {
				if (self->array_index) {
					lua_replace(L, self->array_index);
				}
				self->array_index = 0;
				return SPROTO_CB_NOARRAY;
			}
			if (self->array_index) {
				lua_replace(L, self->array_index);
			} else {
				self->array_index = lua_gettop(L);
			}

			if (map) {
				if (!self->map_entry) {
					lua_createtable(L, 0, 2); // key/value entry
					self->map_entry = lua_gettop(L);
				}
			}

			if (luaL_getmetafield(L, self->array_index, "__pairs")) {
				lua_pushvalue(L, self->array_index);
				lua_call(L,	1, 3);
				int top = lua_gettop(L);
				self->iter_func = top - 2;
				self->iter_table = top - 1;
				self->iter_key = top;
			} else if (!lua_istable(L,self->array_index)) {
				return luaL_error(L, ".*%s(%d) should be a table or an userdata with metamethods (Is a %s)",
					args->tagname, args->index, lua_typename(L, lua_type(L, -1)));
			} else {
				lua_pushnil(L);
				self->iter_func = 0;
				self->iter_table = 0;
				self->iter_key = lua_gettop(L);
			}
		}
		if (args->mainindex >= 0) { // *type(mainindex)
			if (!next_list(L, self)) {
				// iterate end
				lua_pushnil(L);
				lua_replace(L, self->iter_key);
				return SPROTO_CB_NIL;
			}
			if (map) {
				lua_pushvalue(L, -2);
				lua_replace(L, self->iter_key);
				lua_setfield(L, self->map_entry, args->vtagname);
				lua_setfield(L, self->map_entry, args->ktagname);
				lua_pushvalue(L, self->map_entry);
			} else {
				lua_insert(L, -2);
				lua_replace(L, self->iter_key);
			}
		} else {
			lua_geti(L, self->array_index, args->index);
		}
	} else {
		lua_getfield(L, self->tbl_index, args->tagname);
	}
	return 0;
}

static int encode(const struct sproto_arg *args);

static int
encode_one(const struct sproto_arg *args, struct encode_ud *self) {
	lua_State *L = self->L;
	int type = args->type;
	switch (type) {
	case SPROTO_TINTEGER: {
		int64_t v;
		lua_Integer vh;
		int isnum;
		if (args->extra) {
			// It's decimal.
			lua_Number vn = lua_tonumber(L, -1);
			// use 64bit integer for 32bit architecture.
			v = (int64_t)(round(vn * args->extra));
		} else {
			v = tointegerx(L, -1, &isnum);
			if(!isnum) {
				return luaL_error(L, ".%s[%d] is not an integer (Is a %s)", 
					args->tagname, args->index, lua_typename(L, lua_type(L, -1)));
			}
		}
		lua_pop(L,1);
		// notice: in lua 5.2, lua_Integer maybe 52bit
		vh = v >> 31;
		if (vh == 0 || vh == -1) {
			*(uint32_t *)args->value = (uint32_t)v;
			return 4;
		}
		else {
			*(uint64_t *)args->value = (uint64_t)v;
			return 8;
		}
	}
	case SPROTO_TDOUBLE: {
		lua_Number v = lua_tonumber(L, -1);
		*(double*)args->value = (double)v;
		return 8;
	}
	case SPROTO_TBOOLEAN: {
		int isbool;
		int v = tobooleanx(L, -1, &isbool);
		if (!isbool) {
			return luaL_error(L, ".%s[%d] is not a boolean (Is a %s)",
				args->tagname, args->index, lua_typename(L, lua_type(L, -1)));
		}
		*(int *)args->value = v;
		lua_pop(L,1);
		return 4;
	}
	case SPROTO_TSTRING: {
		size_t sz = 0;
		int isstring;
		int type = lua_type(L, -1); // get the type firstly, lua_tolstring may convert value on stack to string
		const char * str = tolstringx(L, -1, &sz, &isstring);
		if (!isstring) {
			return luaL_error(L, ".%s[%d] is not a string (Is a %s)", 
				args->tagname, args->index, lua_typename(L, type));
		}
		if (sz > args->length)
			return SPROTO_CB_ERROR;
		memcpy(args->value, str, sz);
		lua_pop(L,1);
		return sz;
	}
	case SPROTO_TSTRUCT: {
		struct encode_ud sub;
		int r;
		int top = lua_gettop(L);
		sub.L = L;
		sub.st = args->subtype;
		sub.tbl_index = top;
		sub.array_tag = NULL;
		sub.array_index = 0;
		sub.deep = self->deep + 1;
		sub.map_entry = 0;
		sub.iter_func = 0;
		sub.iter_table = 0;
		sub.iter_key = 0;
		r = sproto_encode(args->subtype, args->value, args->length, encode, &sub);
		lua_settop(L, top-1);	// pop the value
		if (r < 0) 
			return SPROTO_CB_ERROR;
		return r;
	}
	default:
		return luaL_error(L, "Invalid field type %d", args->type);
	}
}

static int
encode(const struct sproto_arg *args) {
	struct encode_ud *self = args->ud;
	lua_State *L = self->L;
	int code;
	luaL_checkstack(L, 12, NULL);
	if (self->deep >= ENCODE_DEEPLEVEL)
		return luaL_error(L, "The table is too deep");
	code = get_encodefield(args);
	if (code < 0) {
		return code;
	}
	if (lua_isnil(L, -1)) {
		lua_pop(L,1);
		return SPROTO_CB_NIL;
	}
	return encode_one(args, self);
}

static void *
expand_buffer(lua_State *L, int osz, int nsz) {
	void *output;
	do {
		osz *= 2;
	} while (osz < nsz);
	if (osz > ENCODE_MAXSIZE) {
		luaL_error(L, "object is too large (>%d)", ENCODE_MAXSIZE);
		return NULL;
	}
	output = lua_newuserdata(L, osz);
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
	struct encode_ud self;
	void * buffer = lua_touserdata(L, lua_upvalueindex(1));
	int sz = lua_tointeger(L, lua_upvalueindex(2));
	int tbl_index = 2;
	struct sproto_type * st = lua_touserdata(L, 1);
	if (st == NULL) {
		luaL_checktype(L, tbl_index, LUA_TNIL);
		lua_pushstring(L, "");
		return 1;	// response nil
	}
	self.L = L;
	self.st = st;
	self.tbl_index = tbl_index;
	for (;;) {
		int r;
		self.array_tag = NULL;
		self.array_index = 0;
		self.deep = 0;

		lua_settop(L, tbl_index);
		self.map_entry = 0;
		self.iter_func = 0;
		self.iter_table = 0;
		self.iter_key = 0;

		r = sproto_encode(st, buffer, sz, encode, &self);
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
	int mainindex_tag;
	int key_index;
	int map_entry;
};

static int
decode(const struct sproto_arg *args) {
	struct decode_ud * self = args->ud;
	lua_State *L = self->L;
	if (self->deep >= ENCODE_DEEPLEVEL)
		return luaL_error(L, "The table is too deep");
	luaL_checkstack(L, 12, NULL);
	if (args->index != 0) {
		// It's array
		if (args->tagname != self->array_tag) {
			self->array_tag = args->tagname;
			lua_newtable(L);
			lua_pushvalue(L, -1);
			lua_setfield(L, self->result_index, args->tagname);
			if (self->array_index) {
				lua_replace(L, self->array_index);
			} else {
				self->array_index = lua_gettop(L);
			}
			if (args->index < 0) {
				// It's a empty array, return now.
				return 0;
			}
		}
	}
	switch (args->type) {
	case SPROTO_TINTEGER: {
		// notice: in lua 5.2, 52bit integer support (not 64)
		if (args->extra) {
			// lua_Integer is 32bit in small lua.
			int64_t v = *(int64_t*)args->value;
			lua_Number vn = (lua_Number)v;
			vn /= args->extra;
			lua_pushnumber(L, vn);
		} else {
			int64_t v = *(int64_t*)args->value;
			lua_pushinteger(L, v);
		}
		break;
	}
	case SPROTO_TDOUBLE: {
		double v = *(double*)args->value;
		lua_pushnumber(L, v);
		break;
	}
	case SPROTO_TBOOLEAN: {
		int v = *(uint64_t*)args->value;
		lua_pushboolean(L,v);
		break;
	}
	case SPROTO_TSTRING: {
		lua_pushlstring(L, args->value, args->length);
		break;
	}
	case SPROTO_TSTRUCT: {
		int map = args->ktagname != NULL;
		struct decode_ud sub;
		int r;
		sub.L = L;
		if (map) {
			if (!self->map_entry) {
				lua_newtable(L);
				self->map_entry = lua_gettop(L);
			}
			sub.result_index = self->map_entry;
		} else {
			lua_newtable(L);
			sub.result_index = lua_gettop(L);
		}
		sub.deep = self->deep + 1;
		sub.array_index = 0;
		sub.array_tag = NULL;
		sub.map_entry = 0;
		if (args->mainindex >= 0) {
			// This struct will set into a map, so mark the main index tag.
			sub.mainindex_tag = args->mainindex;
			lua_pushnil(L);
			sub.key_index = lua_gettop(L);

			r = sproto_decode(args->subtype, args->value, args->length, decode, &sub);
			if (r < 0)
				return SPROTO_CB_ERROR;
			if (r != args->length)
				return r;
			if (map) {
				lua_getfield(L, sub.result_index, args->ktagname);
				if (lua_isnil(L, -1)) {
					luaL_error(L, "Can't find key field in [%s]", args->tagname);
				}
				lua_getfield(L, sub.result_index, args->vtagname);
				if (lua_isnil(L, -1)) {
					luaL_error(L, "Can't find value field in [%s]", args->tagname);
				}
				lua_settable(L, self->array_index);
				lua_settop(L, sub.result_index);
			} else {
				lua_pushvalue(L, sub.key_index);
				if (lua_isnil(L, -1)) {
					luaL_error(L, "Can't find main index (tag=%d) in [%s]", args->mainindex, args->tagname);
				}
				lua_pushvalue(L, sub.result_index);
				lua_settable(L, self->array_index);
				lua_settop(L, sub.result_index-1);
			}
			return 0;
		} else {
			sub.mainindex_tag = -1;
			sub.key_index = 0;
			r = sproto_decode(args->subtype, args->value, args->length, decode, &sub);
			if (r < 0)
				return SPROTO_CB_ERROR;
			if (r != args->length)
				return r;
			lua_settop(L, sub.result_index);
			break;
		}
	}
	default:
		luaL_error(L, "Invalid type");
	}
	if (args->index > 0) {
		lua_seti(L, self->array_index, args->index);
	} else {
		if (self->mainindex_tag == args->tagid) {
			// This tag is marked, save the value to key_index
			// assert(self->key_index > 0);
			lua_pushvalue(L,-1);
			lua_replace(L, self->key_index);
		}
		lua_setfield(L, self->result_index, args->tagname);
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
	return table, sz(decoded bytes)
 */
static int
ldecode(lua_State *L) {
	struct sproto_type * st = lua_touserdata(L, 1);
	const void * buffer;
	struct decode_ud self;
	size_t sz;
	int r;
	if (st == NULL) {
		// return nil
		return 0;
	}
	sz = 0;
	buffer = getbuffer(L, 2, &sz);
	if (!lua_istable(L, -1)) {
		lua_newtable(L);
	}
	self.L = L;
	self.result_index = lua_gettop(L);
	self.array_index = 0;
	self.array_tag = NULL;
	self.deep = 0;
	self.mainindex_tag = -1;
	self.key_index = 0;
	self.map_entry = 0;
	r = sproto_decode(st, buffer, (int)sz, decode, &self);
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
	size_t maxsz = (sz + 2047) / 2048 * 2 + sz + 2;
	void * output = lua_touserdata(L, lua_upvalueindex(1));
	int bytes;
	int osz = lua_tointeger(L, lua_upvalueindex(2));
	if (osz < maxsz) {
		output = expand_buffer(L, osz, maxsz);
	}
	bytes = sproto_pack(buffer, sz, output, maxsz);
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
		r = sproto_unpack(buffer, sz, output, r);
		if (r < 0)
			return luaL_error(L, "Invalid unpack stream");
	}
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
	struct sproto_type * request;
	struct sproto_type * response;
	int t;
	int tag;
	if (sp == NULL) {
		return luaL_argerror(L, 1, "Need a sproto_type object");
	}
	t = lua_type(L,2);
	if (t == LUA_TNUMBER) {
		const char * name;
		tag = lua_tointeger(L, 2);
		name = sproto_protoname(sp, tag);
		if (name == NULL)
			return 0;
		lua_pushstring(L, name);
	} else {
		const char * name = lua_tostring(L, 2);
		if (name == NULL) {
			return luaL_argerror(L, 2, "Should be number or string");
		}
		tag = sproto_prototag(sp, name);
		if (tag < 0)
			return 0;
		lua_pushinteger(L, tag);
	}
	request = sproto_protoquery(sp, tag, SPROTO_REQUEST);
	if (request == NULL) {
		lua_pushnil(L);
	} else {
		lua_pushlightuserdata(L, request);
	}
	response = sproto_protoquery(sp, tag, SPROTO_RESPONSE);
	if (response == NULL) {
		if (sproto_protoresponse(sp, tag)) {
			lua_pushlightuserdata(L, NULL);	// response nil
		} else {
			lua_pushnil(L);
		}
	} else {
		lua_pushlightuserdata(L, response);
	}
	return 3;
}

/* global sproto pointer for multi states
   NOTICE : It is not thread safe
 */
static struct sproto * G_sproto[MAX_GLOBALSPROTO];

static int
lsaveproto(lua_State *L) {
	struct sproto * sp = lua_touserdata(L, 1);
	int index = luaL_optinteger(L, 2, 0);
	if (index < 0 || index >= MAX_GLOBALSPROTO) {
		return luaL_error(L, "Invalid global slot index %d", index);
	}
	/* TODO : release old object (memory leak now, but thread safe)*/
	G_sproto[index] = sp;
	return 0;
}

static int
lloadproto(lua_State *L) {
	int index = luaL_optinteger(L, 1, 0);
	struct sproto * sp;
	if (index < 0 || index >= MAX_GLOBALSPROTO) {
		return luaL_error(L, "Invalid global slot index %d", index);
	}
	sp = G_sproto[index];
	if (sp == NULL) {
		return luaL_error(L, "nil sproto at index %d", index);
	}

	lua_pushlightuserdata(L, sp);

	return 1;
}

static void
push_default(const struct sproto_arg *args, int table) {
	lua_State *L = args->ud;
	switch(args->type) {
	case SPROTO_TINTEGER:
		if (args->extra)
			lua_pushnumber(L, 0.0);
		else
			lua_pushinteger(L, 0);
		break;
	case SPROTO_TDOUBLE:
		lua_pushnumber(L, 0.0);
		break;
	case SPROTO_TBOOLEAN:
		lua_pushboolean(L, 0);
		break;
	case SPROTO_TSTRING:
		lua_pushliteral(L, "");
		break;
	case SPROTO_TSTRUCT:
		if (table) {
			lua_pushstring(L, sproto_name(args->subtype));
		} else {
			lua_createtable(L, 0, 1);
			lua_pushstring(L, sproto_name(args->subtype));
			lua_setfield(L, -2, "__type");
		}
		break;
	default:
		luaL_error(L, "Invalid type %d", args->type);
		break;
	}
}

static int
encode_default(const struct sproto_arg *args) {
	lua_State *L = args->ud;
	lua_pushstring(L, args->tagname);
	if (args->index > 0) {
		lua_newtable(L);
		push_default(args, 1);
		lua_setfield(L, -2, "__array");
		lua_rawset(L, -3);
		return SPROTO_CB_NOARRAY;
	} else {
		push_default(args, 0);
		lua_rawset(L, -3);
		return SPROTO_CB_NIL;
	}
}

/*
	lightuserdata sproto_type
	return default table
 */
static int
ldefault(lua_State *L) {
	int ret;
	// 64 is always enough for dummy buffer, except the type has many fields ( > 27).
	char dummy[64];
	struct sproto_type * st = lua_touserdata(L, 1);
	if (st == NULL) {
		return luaL_argerror(L, 1, "Need a sproto_type object");
	}
	lua_newtable(L);
	ret = sproto_encode(st, dummy, sizeof(dummy), encode_default, L);
	if (ret<0) {
		// try again
		int sz = sizeof(dummy) * 2;
		void * tmp = lua_newuserdata(L, sz);
		lua_insert(L, -2);
		for (;;) {
			ret = sproto_encode(st, tmp, sz, encode_default, L);
			if (ret >= 0)
				break;
			sz *= 2;
			tmp = lua_newuserdata(L, sz);
			lua_replace(L, -3);
		}
	}
	return 1;
}

LUAMOD_API int
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
		{ "loadproto", lloadproto },
		{ "saveproto", lsaveproto },
		{ "default", ldefault },
		{ NULL, NULL },
	};
	luaL_newlib(L,l);
	pushfunction_withbuffer(L, "encode", lencode);
	pushfunction_withbuffer(L, "pack", lpack);
	pushfunction_withbuffer(L, "unpack", lunpack);
	return 1;
}
