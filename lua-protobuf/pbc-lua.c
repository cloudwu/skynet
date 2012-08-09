#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>

#include "pbc.h"

#if LUA_VERSION_NUM == 501

#define lua_rawlen lua_objlen
#define luaL_newlib(L ,reg) luaL_register(L,"protobuf.c",reg)
#define luaL_buffinit(L , _ ) 
#define luaL_prepbuffsize( b , cap ) malloc(cap)
#define _Free(p) free(p)
#undef luaL_addsize
#define luaL_addsize(b , len) lua_pushlstring(L, temp , len) ; free(temp)
#define luaL_pushresult(b) 
#define luaL_checkversion(L)

#else

#define _Free(p)

#endif

static inline void *
checkuserdata(lua_State *L, int index) {
	void * ud = lua_touserdata(L,index);
	if (ud == NULL) {
		luaL_error(L, "userdata %d is nil",index);
	}
	return ud;
}

static int
_env_new(lua_State *L) {
	struct pbc_env * env = pbc_new();
	lua_pushlightuserdata(L, env);
	return 1;
}

static int
_env_delete(lua_State *L) {
	struct pbc_env * env = lua_touserdata(L,1);
	pbc_delete(env);

	return 0;
}

static int
_env_register(lua_State *L) {
	struct pbc_env * env = checkuserdata(L,1);
	size_t sz = 0;
	const char * buffer = luaL_checklstring(L, 2 , &sz);
	struct pbc_slice slice;
	slice.buffer = (void *)buffer;
	slice.len = (int)sz;
	int ret = pbc_register(env, &slice);

	if (ret) {
		return luaL_error(L, "register fail");
	}
	return 0;
}

static int
_rmessage_new(lua_State *L) {
	struct pbc_env * env = checkuserdata(L,1);
	const char * typename = luaL_checkstring(L,2);
	struct pbc_slice slice;
	if (lua_isstring(L,3)) {
		size_t sz = 0;
		slice.buffer = (void *)lua_tolstring(L,3,&sz);
		slice.len = (int)sz;
	} else {
		slice.buffer = lua_touserdata(L,3);
		slice.len = luaL_checkinteger(L,4);
	}
	struct pbc_rmessage * m = pbc_rmessage_new(env, typename, &slice);
	if (m==NULL)
		return 0;
	lua_pushlightuserdata(L,m);
	return 1;
}

static int
_rmessage_delete(lua_State *L) {
	struct pbc_rmessage * m = checkuserdata(L,1);
	pbc_rmessage_delete(m);

	return 0;
}

static int
_rmessage_integer(lua_State *L) {
	struct pbc_rmessage * m = checkuserdata(L,1);
	const char * key = luaL_checkstring(L,2);
	int index = luaL_checkinteger(L,3);
	int32_t v = (int32_t)pbc_rmessage_integer(m, key, index, NULL);

	lua_pushinteger(L,v);

	return 1;
}

static int
_rmessage_int32(lua_State *L) {
	struct pbc_rmessage * m = checkuserdata(L,1);
	const char * key = luaL_checkstring(L,2);
	int index = luaL_checkinteger(L,3);
	uint32_t v = pbc_rmessage_integer(m, key, index, NULL);
	lua_pushlightuserdata(L,(void *)(intptr_t)v);

	return 1;
}

static int
_rmessage_int64_ia64(lua_State *L) {
	struct pbc_rmessage * m = checkuserdata(L,1);
	const char * key = luaL_checkstring(L,2);
	int index = luaL_checkinteger(L,3);
	uint32_t v[2];
	v[0] = pbc_rmessage_integer(m, key, index, &v[1]);
	uint64_t u64 = (uint64_t)v[0] | (uint64_t)v[1] << 32;

	lua_pushlightuserdata(L,(void *)u64);

	return 1;
}

static int
_rmessage_int52(lua_State *L) {
	struct pbc_rmessage * m = checkuserdata(L,1);
	const char * key = luaL_checkstring(L,2);
	int index = luaL_checkinteger(L,3);
	uint32_t hi,low;
	low = pbc_rmessage_integer(m, key, index, &hi);
	int64_t v = (int64_t)((uint64_t)hi << 32 | (uint64_t)low);
	lua_pushnumber(L,(lua_Number)v);

	return 1;
}

static int
_rmessage_uint52(lua_State *L) {
	struct pbc_rmessage * m = checkuserdata(L,1);
	const char * key = luaL_checkstring(L,2);
	int index = luaL_checkinteger(L,3);
	uint32_t hi,low;
	low = pbc_rmessage_integer(m, key, index, &hi);
	uint64_t v = (uint64_t)hi << 32 | (uint64_t)low;
	lua_pushnumber(L,(lua_Number)v);

	return 1;
}

static int 
_rmessage_real(lua_State *L) {
	struct pbc_rmessage * m = checkuserdata(L,1);
	const char * key = luaL_checkstring(L,2);
	int index = luaL_checkinteger(L,3);
	double v = pbc_rmessage_real(m, key, index);

	lua_pushnumber(L,v);

	return 1;
}

static int
_rmessage_string(lua_State *L) {
	struct pbc_rmessage * m = checkuserdata(L,1);
	const char * key = luaL_checkstring(L,2);
	int index = lua_tointeger(L,3);
	int sz = 0;
	const char * v = pbc_rmessage_string(m,key,index,&sz);
	lua_pushlstring(L,v,sz);
	return 1;
}

static int
_rmessage_message(lua_State *L) {
	struct pbc_rmessage * m = checkuserdata(L,1);
	const char * key = luaL_checkstring(L,2);
	int index = lua_tointeger(L,3);
	struct pbc_rmessage * v = pbc_rmessage_message(m,key,index);
	lua_pushlightuserdata(L,v);
	return 1;
}

static int
_rmessage_size(lua_State *L) {
	struct pbc_rmessage * m = checkuserdata(L,1);
	const char * key = luaL_checkstring(L,2);

	int sz = pbc_rmessage_size(m, key);

	lua_pushinteger(L, sz);

	return 1;
}

static int
_env_type(lua_State *L) {
	lua_settop(L,3);
	struct pbc_env * env = checkuserdata(L,1);
	const char * typename = luaL_checkstring(L,2);
	if (lua_isnil(L,3)) {
		int ret = pbc_type(env, typename, NULL, NULL);
		lua_pushboolean(L,ret);
		return 1;
	}
	const char * key = luaL_checkstring(L,3);
	const char * type = NULL;
	int ret = pbc_type(env, typename, key, &type);
	lua_pushinteger(L,ret);
	if (type == NULL) {
		return 1;
	} {
		lua_pushstring(L, type);
		return 2;
	}
}

static int
_wmessage_new(lua_State *L) {
	struct pbc_env * env = checkuserdata(L,1);
	const char * typename = luaL_checkstring(L,2);
	struct pbc_wmessage * ret = pbc_wmessage_new(env, typename);
	lua_pushlightuserdata(L,ret);
	return 1;
}

static int
_wmessage_delete(lua_State *L) {
	struct pbc_wmessage * m = lua_touserdata(L,1);
	pbc_wmessage_delete(m);

	return 0;
}


static int
_wmessage_integer(lua_State *L) {
	struct pbc_wmessage * m = checkuserdata(L,1);
	const char * key = luaL_checkstring(L,2);
	int number = luaL_checkinteger(L,3);
	uint32_t hi = 0;
	if (number < 0)
		hi = ~0;
	pbc_wmessage_integer(m, key, number, hi);

	return 0;
}

static int
_wmessage_real(lua_State *L) {
	struct pbc_wmessage * m = checkuserdata(L,1);
	const char * key = luaL_checkstring(L,2);
	double number = luaL_checknumber(L,3);
	pbc_wmessage_real(m, key, number);

	return 0;
}

static int
_wmessage_string(lua_State *L) {
	struct pbc_wmessage * m = checkuserdata(L,1);
	const char * key = luaL_checkstring(L,2);
	size_t len = 0;
	const char * v = luaL_checklstring(L,3,&len);
	int err = pbc_wmessage_string(m, key, v, (int)len);
	if (err) {
		return luaL_error(L, "Invalid enum %s", v);
	}

	return 0;
}

static int
_wmessage_message(lua_State *L) {
	struct pbc_wmessage * m = checkuserdata(L,1);
	const char * key = luaL_checkstring(L,2);
	struct pbc_wmessage * ret = pbc_wmessage_message(m, key);
	lua_pushlightuserdata(L, ret);

	return 1;
}

static int
_wmessage_int32(lua_State *L) {
	struct pbc_wmessage * m = checkuserdata(L,1);
	const char * key = luaL_checkstring(L,2);
	if (!lua_islightuserdata(L,3)) {
		return luaL_error(L,"Need a lightuserdata for int32");
	}
	void *number = lua_touserdata(L,3);
	pbc_wmessage_integer(m, key, (uint32_t)(intptr_t)number , 0);
	return 0;
}

static int
_wmessage_int64_ia64(lua_State *L) {
	struct pbc_wmessage * m = checkuserdata(L,1);
	const char * key = luaL_checkstring(L,2);
	luaL_checktype(L, 3, LUA_TLIGHTUSERDATA);
	void * v = lua_touserdata(L,3);
	uint64_t v64 = (uintptr_t)v;
	pbc_wmessage_integer(m, key, (uint32_t)v64 , (uint32_t)(v64>>32));
	return 0;
}

static int
_wmessage_int52(lua_State *L) {
	struct pbc_wmessage * m = checkuserdata(L,1);
	const char * key = luaL_checkstring(L,2);
	int64_t number = (int64_t)(luaL_checknumber(L,3));
	uint32_t hi = (uint32_t)(number >> 32);
	pbc_wmessage_integer(m, key, (uint32_t)number, hi);

	return 0;
}

static int
_wmessage_uint52(lua_State *L) {
	struct pbc_wmessage * m = checkuserdata(L,1);
	const char * key = luaL_checkstring(L,2);
	lua_Number v = (luaL_checknumber(L,3));
	if (v < 0) {
		return luaL_error(L, "negative number : %f passed to unsigned field",v);
	}
	uint64_t number = (uint64_t)v;
	uint32_t hi = (uint32_t)(number >> 32);
	pbc_wmessage_integer(m, key, (uint32_t)number, hi);

	return 0;
}

static int
_wmessage_buffer(lua_State *L) {
	struct pbc_slice slice;
	struct pbc_wmessage * m = checkuserdata(L,1);
	pbc_wmessage_buffer(m , &slice);
	lua_pushlightuserdata(L, slice.buffer);
	lua_pushinteger(L, slice.len);
	return 2;
}

static int
_wmessage_buffer_string(lua_State *L) {
	struct pbc_slice slice;
	struct pbc_wmessage * m = checkuserdata(L,1);
	pbc_wmessage_buffer(m , &slice);
	lua_pushlstring(L, (const char *)slice.buffer, slice.len);
	return 1;
}

/*
	lightuserdata env
 */
static int
_last_error(lua_State *L) {
	struct pbc_env * env = checkuserdata(L, 1);
	const char * err = pbc_error(env);
	lua_pushstring(L,err);
	return 1;
}

/*
	lightuserdata env
	string message
	string format
 */
static int
_pattern_new(lua_State *L) {
	struct pbc_env * env = checkuserdata(L, 1);
	const char * message = luaL_checkstring(L,2);
	const char * format = luaL_checkstring(L,3);
	struct pbc_pattern * pat = pbc_pattern_new(env, message, format);
	if (pat == NULL) {
		return luaL_error(L, "create patten %s (%s) failed", message , format);
	}
	lua_pushlightuserdata(L,pat);

	return 1;
}

static int
_pattern_delete(lua_State *L) {
	struct pbc_pattern * pat = lua_touserdata(L,1);
	pbc_pattern_delete(pat);
	
	return 0;
}

static void *
_push_value(lua_State *L, char * ptr, char type) {
	switch(type) {
		case 'u': {
			uint64_t v = *(uint64_t*)ptr;
			ptr += 8;
			lua_pushnumber(L,(lua_Number)v);
			break;
		}
		case 'i': {
			int32_t v = *(int32_t*)ptr;
			ptr += 4;
			lua_pushinteger(L,v);
			break;
		}
		case 'b': {
			int32_t v = *(int32_t*)ptr;
			ptr += 4;
			lua_pushboolean(L,v);
			break;
		}
		case 'p': {
			uint32_t v = *(uint32_t*)ptr;
			ptr += 4;
			lua_pushlightuserdata(L,(void *)(intptr_t)v);
			break;
		}
		case 'x': {
			void * v;
			memcpy(&v, ptr, 8);
			lua_pushlightuserdata(L,v);
			ptr += 8;
			break;
		}
		case 'd': {
			int64_t v = *(int64_t*)ptr;
			ptr += 8;
			lua_pushnumber(L,(lua_Number)v);
			break;
		}
		case 'r': {
			double v = *(double *)ptr;
			ptr += 8;
			lua_pushnumber(L,v);
			break;
		}
		case 's': {
			struct pbc_slice * slice = (struct pbc_slice *)ptr;
			lua_pushlstring(L,slice->buffer, slice->len);
			ptr += sizeof(struct pbc_slice);
			break;
		}
		case 'm': {
			struct pbc_slice * slice = (struct pbc_slice *)ptr;
			lua_createtable(L,2,0);
			lua_pushlightuserdata(L, slice->buffer);
			lua_rawseti(L,-2,1);
			lua_pushinteger(L,slice->len);
			lua_rawseti(L,-2,2);
			ptr += sizeof(struct pbc_slice);
			break;			
		}
	}
	return ptr;
}

static void
_push_array(lua_State *L, pbc_array array, char type, int index) {
	switch (type) {
	case 'I': {
		int v = pbc_array_integer(array, index, NULL);
		lua_pushinteger(L, v);
		break;
	}
	case 'U': {
		uint32_t hi = 0;
		uint32_t low = pbc_array_integer(array, index, &hi);
		uint64_t v = (uint64_t)hi << 32 | (uint64_t)low;
		lua_pushnumber(L, (lua_Number)v);
		break;
	}
	case 'D': {
		uint32_t hi = 0;
		uint32_t low = pbc_array_integer(array, index, &hi);
		uint64_t v = (uint64_t)hi << 32 | (uint64_t)low;
		lua_pushnumber(L, (lua_Number)((int64_t)v));
		break;
	}
	case 'B': {
		int v = pbc_array_integer(array, index, NULL);
		lua_pushboolean(L, v);
		break;
	}
	case 'P': {
		uint32_t v = pbc_array_integer(array, index, NULL);
		lua_pushlightuserdata(L,(void *)(intptr_t)v);
		break;
	}
	case 'X': {
		uint32_t hi = 0;
		uint32_t low = pbc_array_integer(array, index, &hi);
		uint64_t v = (uint64_t)low | (uint64_t)hi << 32;
		lua_pushlightuserdata(L, (void *)v);
		break;
	}
	case 'R': {
		double v = pbc_array_real(array, index);
		lua_pushnumber(L, v);
		break;
	}
	case 'S': {
		struct pbc_slice * slice = pbc_array_slice(array, index);
		lua_pushlstring(L, (const char *)slice->buffer,slice->len);
		break;
	}
	case 'M': {
		struct pbc_slice * slice = pbc_array_slice(array, index);
		lua_createtable(L,2,0);
		lua_pushlightuserdata(L,slice->buffer);
		lua_rawseti(L,-2,1);
		lua_pushinteger(L,slice->len);
		lua_rawseti(L,-2,2);
		break;
	}
	}
	lua_rawseti(L,-2,index+1);
}

/*
	lightuserdata pattern
	string format "ixrsmb"
	integer size
	lightuserdata buffer
	integer buffer_len
 */
static int
_pattern_unpack(lua_State *L) {
	struct pbc_pattern * pat = checkuserdata(L, 1);
	if (pat == NULL) {
		return luaL_error(L, "unpack pattern is NULL");
	}
	size_t format_sz = 0;
	const char * format = lua_tolstring(L,2,&format_sz);
	int size = lua_tointeger(L,3);
	struct pbc_slice slice;
	if (lua_isstring(L,4)) {
		size_t buffer_len = 0;
		const char *buffer = luaL_checklstring(L,4,&buffer_len);
		slice.buffer = (void *)buffer;
		slice.len = buffer_len;
	} else {
		if (!lua_isuserdata(L,4)) {
			return luaL_error(L, "Need a userdata");
		}
		slice.buffer = lua_touserdata(L,4);
		slice.len = luaL_checkinteger(L,5);
	}
	
	char temp[size];
	int ret = pbc_pattern_unpack(pat, &slice, temp);
	if (ret < 0) 
		return 0;
	lua_checkstack(L, format_sz + 3);
	int i;
	char * ptr = temp;
	bool array = false;
	for (i=0;i<format_sz;i++) {
		char type = format[i];
		if (type >= 'a' && type <='z') {
			ptr = _push_value(L,ptr,type);
		} else {
			array = true;
			int n = pbc_array_size((void *)ptr);
			lua_createtable(L,n,0);
			int j;
			for (j=0;j<n;j++) {
				_push_array(L,(void *)ptr, type, j);
			}
		}
	}
	if (array) {
		pbc_pattern_close_arrays(pat, temp);
	}
	return format_sz;
}

static char *
_get_value(lua_State *L, int index, char * ptr, char type) {
	switch(type) {
		case 'i': {
			int32_t v = luaL_checkinteger(L, index);
			memcpy(ptr, &v, 4);
			return ptr + 4;
		}
		case 'u': {
			uint64_t v = (uint64_t)luaL_checknumber(L, index);
			memcpy(ptr, &v, 8);
			return ptr + 8;
		}
		case 'd': {
			int64_t v = (int64_t)luaL_checknumber(L, index);
			memcpy(ptr, &v, 8);
			return ptr + 8;
		}
		case 'b': {
			int32_t v = lua_toboolean(L, index);
			memcpy(ptr, &v, 4);
			return ptr + 4;
		}
		case 'p': {
			void *p = lua_touserdata(L, index);
			uint32_t v = (uint32_t)(intptr_t)p;
			memcpy(ptr, &v , 4);
			return ptr + 4;
		}
		case 'x': {
			luaL_checktype(L,index,LUA_TLIGHTUSERDATA);
			void * i64 = lua_touserdata(L,index);
			memcpy(ptr, &i64, 8);
			return ptr + 8;
		}
		case 'r': {
			double v = luaL_checknumber(L, index);
			memcpy(ptr, &v, 8);
			return ptr + 8;
		}
		case 's': {
			size_t sz = 0;
			const char * str = luaL_checklstring(L, index, &sz);
			struct pbc_slice * slice = (struct pbc_slice *)ptr;
			slice->buffer = (void*)str;
			slice->len = sz;
			return ptr + sizeof(struct pbc_slice);
		}
		case 'm': {
			struct pbc_slice * slice = (struct pbc_slice *)ptr;
			if (lua_istable(L,index)) {
				lua_rawgeti(L,index,1);
				slice->buffer = lua_touserdata(L,-1);
				lua_rawgeti(L,index,2);
				slice->len = lua_tointeger(L,-1);
				lua_pop(L,2);
			} else {
				size_t sz = 0;
				const char * buffer = luaL_checklstring(L, index, &sz);
				slice->buffer = (void *)buffer;
				slice->len = sz;
			}
			return ptr + sizeof(struct pbc_slice);
		}
		default:
			luaL_error(L,"unknown format %c", type);
			return ptr;
	}
}

static void
_get_array_value(lua_State *L, pbc_array array, char type) {
	switch(type) {
		case 'I': {
			int32_t v = luaL_checkinteger(L, -1);
			uint32_t hi = 0;
			if (v<0) {
				hi = ~0;
			}
			pbc_array_push_integer(array, v, hi);
			break;
		}
		case 'U' : {
			uint64_t v = (uint64_t)luaL_checknumber(L, -1);
			pbc_array_push_integer(array, (uint32_t)v, (uint32_t)(v >> 32));
			break;
		}
		case 'D' : {
			int64_t v = (int64_t)luaL_checknumber(L, -1);
			pbc_array_push_integer(array, (uint32_t)v, (uint32_t)(v >> 32));
			break;
		}
		case 'B': {
			int32_t v = lua_toboolean(L, -1);
			pbc_array_push_integer(array, v ? 1: 0, 0);
			break;
		}
		case 'P': {
			void *p = lua_touserdata(L, -1);
			uint32_t v = (uint32_t)(intptr_t)p;
			pbc_array_push_integer(array, v, 0);
			break;
		}
		case 'X': {
			luaL_checktype(L,-1,LUA_TLIGHTUSERDATA);
			void * i64 = lua_touserdata(L,-1);
			uint64_t v = (uintptr_t)i64;
			pbc_array_push_integer(array, (uint32_t)v, (uint32_t)(v >> 32));
			break;
		}
		case 'R': {
			double v = luaL_checknumber(L, -1);
			pbc_array_push_real(array, v);
			break;
		}
		case 'S': {
			size_t sz = 0;
			const char * str = luaL_checklstring(L, -1, &sz);
			struct pbc_slice slice;
			slice.buffer = (void*)str;
			slice.len = sz;
			pbc_array_push_slice(array, &slice);
			break;
		}
		case 'M': {
			struct pbc_slice slice;
			if (lua_istable(L,-1)) {
				lua_rawgeti(L,-1,1);
				slice.buffer = lua_touserdata(L,-1);
				lua_rawgeti(L,-2,2);
				slice.len = lua_tointeger(L,-1);
				lua_pop(L,2);
			} else {
				size_t sz = 0;
				const char * buffer = luaL_checklstring(L, -1, &sz);
				slice.buffer = (void *)buffer;
				slice.len = sz;
			}
			pbc_array_push_slice(array, &slice);
			break;
		}
	}
}

/*
	lightuserdata pattern
	string format "ixrsmbp"
	integer size
 */
static int
_pattern_pack(lua_State *L) {
	struct pbc_pattern * pat = checkuserdata(L,1);
	if (pat == NULL) {
		return luaL_error(L, "pack pattern is NULL");
	}
	size_t format_sz = 0;
	const char * format = lua_tolstring(L,2,&format_sz);
	int size = lua_tointeger(L,3);

	char data[size];
//	A trick , we don't need default value. zero buffer for array and message field.
//	pbc_pattern_set_default(pat, data);
	memset(data, 0 , size);

	char * ptr = data;

	int i;

	for (i=0;i<format_sz;i++) {
		if (format[i] >= 'a' && format[i] <='z') {
			ptr = _get_value(L, 4+i, ptr, format[i]);
		} else {
			if (!lua_istable(L,4+i)) {
				luaL_error(L,"need table for array type");
			}
			int j;
			int n = lua_rawlen(L,4+i);
			for (j=0;j<n;j++) {
				lua_rawgeti(L,4+i,j+1);
				_get_array_value(L,(void *)ptr,format[i]);
				lua_pop(L,1);
			}
			ptr += sizeof(pbc_array);
		}
	}

	luaL_Buffer b;
	luaL_buffinit(L, &b);

	int cap = 128;
	for (;;) {
		char * temp = luaL_prepbuffsize(&b , cap);

		struct pbc_slice slice;
		slice.buffer = temp;
		slice.len = cap;

		int ret = pbc_pattern_pack(pat, data, &slice);

		if (ret < 0) {
			cap = cap * 2;
			_Free(temp);
			continue;
		}

		luaL_addsize(&b , slice.len);
		break;
	}
	luaL_pushresult(&b);

	pbc_pattern_close_arrays(pat, data);

	return 1;
}

static int
_pattern_size(lua_State *L) {
	size_t sz =0;
	const char *format = luaL_checklstring(L,1,&sz);
	int i;
	int size = 0;
	for (i=0;i<sz;i++) {
		switch(format[i]) {
		case 'b': 
		case 'i':
		case 'p':
			size += 4;
			break;
		case 'r':
		case 'x': 
		case 'u':
		case 'd':
			size += 8;
			break;
		case 's':
		case 'm':
			size += sizeof(struct pbc_slice);
			break;
		default:
			size += sizeof(pbc_array);
			break;
		}
	}
	lua_pushinteger(L,size);
	return 1;
}

/*
	-3 table key
	-2 table id
	-1 value
 */
static void
new_array(lua_State *L, int id, const char *key) {
	lua_rawgeti(L, -2 , id);
	if (lua_isnil(L, -1)) {
		lua_pop(L,1);
		lua_newtable(L);  // table.key table.id value array
		lua_pushvalue(L,-1);
		lua_pushvalue(L,-1); // table.key table.id value array array array
		lua_setfield(L, -6 , key);
		lua_rawseti(L, -4, id);
	}
}

static void
push_value(lua_State *L, int type, const char * typename, union pbc_value *v) {
	switch(type) {
	case PBC_INT:
		lua_pushinteger(L, (int)v->i.low);
		break;
	case PBC_REAL:
		lua_pushnumber(L, v->f);
		break;
	case PBC_BOOL:
		lua_pushboolean(L, v->i.low);
		break;
	case PBC_ENUM:
		lua_pushstring(L, v->e.name);
		break;
	case PBC_BYTES:
	case PBC_STRING:
		lua_pushlstring(L, (const char *)v->s.buffer , v->s.len);
		break;
	case PBC_MESSAGE:
		lua_pushvalue(L, -3);
		lua_pushstring(L, typename);
		lua_pushlstring(L, (const char *)v->s.buffer , v->s.len);
		lua_call(L, 2 , 1);
		break;
	case PBC_FIXED64:
		lua_pushlstring(L, (const char *)&(v->i), 8);
		break;
	case PBC_FIXED32:
		lua_pushlightuserdata(L,(void *)(intptr_t)v->i.low);
		break;
	case PBC_INT64: {
		uint64_t v64 = (uint64_t)(v->i.hi) << 32 | (uint64_t)(v->i.low);
		lua_pushnumber(L,(lua_Number)(int64_t)v64);
		break;
	}
	case PBC_UINT: {
		uint64_t v64 = (uint64_t)(v->i.hi) << 32 | (uint64_t)(v->i.low);
		lua_pushnumber(L,(lua_Number)v64);
		break;
	}
	default:
		luaL_error(L, "Unknown type %s", typename);
		break;
	}
}

/*
	-3: function decode
	-2: table key
	-1:	table id
 */
static void
decode_cb(void *ud, int type, const char * typename, union pbc_value *v, int id, const char *key) {
	lua_State *L = ud;
	if (type & PBC_REPEATED) {
		push_value(L, type & ~PBC_REPEATED, typename, v);
		new_array(L, id , key);	// func.decode table.key table.id value array
		int n = lua_rawlen(L,-1);
		lua_insert(L, -2);	// func.decode table.key table.id array value
		lua_rawseti(L, -2 , n+1);	// func.decode table.key table.id array
		lua_pop(L,1);
	} else {
		push_value(L, type, typename, v);
		lua_setfield(L, -3 , key);
	}
}

/*
	:1 lightuserdata env
	:2 function decode_message
	:3 table target
	:4 string type
	:5 string data
	:5 lightuserdata pointer
	:6 integer len

	table
 */
static int
_decode(lua_State *L) {
	struct pbc_env * env = checkuserdata(L,1);
	luaL_checktype(L, 2 , LUA_TFUNCTION);
	luaL_checktype(L, 3 , LUA_TTABLE);
	const char * type = luaL_checkstring(L,4);
	struct pbc_slice slice;
	if (lua_type(L,5) == LUA_TSTRING) {
		size_t len;
		slice.buffer = (void *)luaL_checklstring(L,5,&len);
		slice.len = (int)len;
	} else {
		slice.buffer = checkuserdata(L,5);
		slice.len = luaL_checkinteger(L,6);
	}
	lua_pushvalue(L, 2);
	lua_pushvalue(L, 3);
	lua_newtable(L);

	int n = pbc_decode(env, type, &slice, decode_cb, L);
	if (n<0) {
		lua_pushboolean(L,0);
	} else {
		lua_pushboolean(L,1);
	}
	return 1;
}

int
luaopen_protobuf_c(lua_State *L) {
	luaL_Reg reg[] = {
		{"_env_new" , _env_new },
		{"_env_delete" , _env_delete },
		{"_env_register" , _env_register },
		{"_env_type", _env_type },
		{"_rmessage_new" , _rmessage_new },
		{"_rmessage_delete" , _rmessage_delete },
		{"_rmessage_integer" , _rmessage_integer },
		{"_rmessage_int32", _rmessage_int32 },
		{"_rmessage_int64", _rmessage_int64_ia64 },
		{"_rmessage_int52", _rmessage_int52 },
		{"_rmessage_uint52", _rmessage_uint52 },
		{"_rmessage_real" , _rmessage_real },
		{"_rmessage_string" , _rmessage_string },
		{"_rmessage_message" , _rmessage_message },
		{"_rmessage_size" , _rmessage_size },
		{"_wmessage_new", _wmessage_new },
		{"_wmessage_delete", _wmessage_delete },
		{"_wmessage_integer", _wmessage_integer },
		{"_wmessage_real", _wmessage_real },
		{"_wmessage_string", _wmessage_string },
		{"_wmessage_message", _wmessage_message },
		{"_wmessage_int32", _wmessage_int32 },
		{"_wmessage_int64", _wmessage_int64_ia64 },
		{"_wmessage_int52", _wmessage_int52 },
		{"_wmessage_uint52", _wmessage_uint52 },
		{"_wmessage_buffer", _wmessage_buffer },
		{"_wmessage_buffer_string", _wmessage_buffer_string },
		{"_pattern_new", _pattern_new },
		{"_pattern_delete", _pattern_delete },
		{"_pattern_size", _pattern_size },
		{"_pattern_unpack", _pattern_unpack },
		{"_pattern_pack", _pattern_pack },
		{"_last_error", _last_error },
		{"_decode", _decode },
		{NULL,NULL},
	};

	if (sizeof(void *) != sizeof(uint64_t)) {
		luaL_error(L, "Skynet need ia64");
	}

	luaL_checkversion(L);
	luaL_newlib(L, reg);

	return 1;
}
