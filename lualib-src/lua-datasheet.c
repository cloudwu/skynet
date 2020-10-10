#include <lua.h>
#include <lauxlib.h>
#include <stdint.h>

#define NODECACHE "_ctable"
#define PROXYCACHE "_proxy"
#define TABLES "_ctables"

#define VALUE_NIL 0
#define VALUE_INTEGER 1
#define VALUE_REAL 2
#define VALUE_BOOLEAN 3
#define VALUE_TABLE 4
#define VALUE_STRING 5
#define VALUE_INVALID 6

#define INVALID_OFFSET 0xffffffff

struct proxy {
	const char * data;
	int index;
};

struct document {
	uint32_t strtbl;
	uint32_t n;
	uint32_t index[1];
	// table[n]
	// strings
};

struct table {
	uint32_t array;
	uint32_t dict;
	uint8_t type[1];
	// value[array]
	// kvpair[dict]
};

static inline const struct table *
gettable(const struct document *doc, int index) {
	if (doc->index[index] == INVALID_OFFSET) {
		return NULL;
	}
	return (const struct table *)((const char *)doc + sizeof(uint32_t) + sizeof(uint32_t) + doc->n * sizeof(uint32_t) + doc->index[index]);
}

static void
create_proxy(lua_State *L, const void *data, int index) {
	const struct table * t = gettable(data, index);
	if (t == NULL) {
		luaL_error(L, "Invalid index %d", index);
	}
	lua_getfield(L, LUA_REGISTRYINDEX, NODECACHE);
	if (lua_rawgetp(L, -1, t) == LUA_TTABLE) {
		lua_replace(L, -2);
		return;
	}
	lua_pop(L, 1);
	lua_newtable(L);
	lua_pushvalue(L, lua_upvalueindex(1));
	lua_setmetatable(L, -2);
	lua_pushvalue(L, -1);
	// NODECACHE, table, table
	lua_rawsetp(L, -3, t);
	// NODECACHE, table
	lua_getfield(L, LUA_REGISTRYINDEX, PROXYCACHE);
	// NODECACHE, table, PROXYCACHE
	lua_pushvalue(L, -2);
	// NODECACHE, table, PROXYCACHE, table
	struct proxy * p = lua_newuserdatauv(L, sizeof(struct proxy), 0);
	// NODECACHE, table, PROXYCACHE, table, proxy
	p->data = data;
	p->index = index;
	lua_rawset(L, -3);
	// NODECACHE, table, PROXYCACHE
	lua_pop(L, 1);
	// NODECACHE, table
	lua_replace(L, -2);
	// table
}

static void
clear_table(lua_State *L) {
	int t = lua_gettop(L);	// clear top table
	if (lua_type(L, t) != LUA_TTABLE) {
		luaL_error(L, "Invalid cache");
	}
	lua_pushnil(L);
	while (lua_next(L, t) != 0) {
		// key value
		lua_pop(L, 1);
		lua_pushvalue(L, -1);
		lua_pushnil(L);
		// key key nil
		lua_rawset(L, t);
		// key
	}
}

static void
update_cache(lua_State *L, const void *data, const void * newdata) {
	lua_getfield(L, LUA_REGISTRYINDEX, NODECACHE);
	int t = lua_gettop(L);
	lua_getfield(L, LUA_REGISTRYINDEX, PROXYCACHE);
	int pt = t + 1;
	lua_newtable(L);	// temp table
	int nt = pt + 1;
	lua_pushnil(L);
	while (lua_next(L, t) != 0) {
		// pointer (-2) -> table (-1)
		lua_pushvalue(L, -1);
		if (lua_rawget(L, pt) == LUA_TUSERDATA) {
			// pointer, table, proxy
			struct proxy * p = lua_touserdata(L, -1);
			if (p->data == data) {
				// update to newdata
				p->data = newdata;
				const struct table * newt = gettable(newdata, p->index);
				lua_pop(L, 1);
				// pointer, table
				clear_table(L);
				lua_pushvalue(L, lua_upvalueindex(1));
				// pointer, table, meta
				lua_setmetatable(L, -2);
				// pointer, table
				if (newt) {
					lua_rawsetp(L, nt, newt);
				} else {
					lua_pop(L, 1);
				}
				// pointer
				lua_pushvalue(L, -1);
				lua_pushnil(L);
				lua_rawset(L, t);
			} else {
				lua_pop(L, 2);
			}
		} else {
			lua_pop(L, 2);
			// pointer
		}
	}
	// copy nt to t
	lua_pushnil(L);
	while (lua_next(L, nt) != 0) {
		lua_pushvalue(L, -2);
		lua_insert(L, -2);
		// key key value
		lua_rawset(L, t);
	}
	// NODECACHE PROXYCACHE TEMP
	lua_pop(L, 3);
}

static int
lupdate(lua_State *L) {
	lua_getfield(L, LUA_REGISTRYINDEX, PROXYCACHE);
	lua_pushvalue(L, 1);
	// PROXYCACHE, table
	if (lua_rawget(L, -2) != LUA_TUSERDATA) {
		luaL_error(L, "Invalid proxy table %p", lua_topointer(L, 1));
	}
	struct proxy * p = lua_touserdata(L, -1);
	luaL_checktype(L, 2, LUA_TLIGHTUSERDATA);
	const char * newdata = lua_touserdata(L, 2);
	update_cache(L, p->data, newdata);
	return 1;
}

static inline uint32_t
getuint32(const void *v) {
	union {
		uint32_t d;
		uint8_t t[4];
	} test = { 1 };
	if (test.t[0] == 0) {
		// big endian
		test.d = *(const uint32_t *)v;
		return test.t[0] | test.t[1] << 4 | test.t[2] << 8 | test.t[3] << 12;
	} else {
		return *(const uint32_t *)v;
	}
}

static inline float
getfloat(const void *v) {
	union {
		uint32_t d;
		float f;
		uint8_t t[4];
	} test = { 1 };
	if (test.t[0] == 0) {
		// big endian
		test.d = *(const uint32_t *)v;
		test.d = test.t[0] | test.t[1] << 4 | test.t[2] << 8 | test.t[3] << 12;
		return test.f;
	} else {
		return *(const float *)v;
	}
}

static void
pushvalue(lua_State *L, const void *v, int type, const struct document * doc) {
	switch (type) {
	case VALUE_NIL:
		lua_pushnil(L);
		break;
	case VALUE_INTEGER:
		lua_pushinteger(L, (int32_t)getuint32(v));
		break;
	case VALUE_REAL:
		lua_pushnumber(L, getfloat(v));
		break;
	case VALUE_BOOLEAN:
		lua_pushboolean(L, getuint32(v));
		break;
	case VALUE_TABLE:
		create_proxy(L, doc, getuint32(v));
		break;
	case VALUE_STRING:
		lua_pushstring(L,  (const char *)doc + doc->strtbl + getuint32(v));
		break;
	default:
		luaL_error(L, "Invalid type %d at %p", type, v);
	}
}

static void
copytable(lua_State *L, int tbl, struct proxy *p) {
	const struct document * doc = (const struct document *)p->data; 
	if (p->index < 0 || p->index >= doc->n) {
		luaL_error(L, "Invalid proxy (index = %d, total = %d)", p->index, (int)doc->n);
	}
	const struct table * t = gettable(doc, p->index);
	if (t == NULL) {
		luaL_error(L, "Invalid proxy (index = %d)", p->index);
	}
	const uint32_t * v = (const uint32_t *)((const char *)t + sizeof(uint32_t) + sizeof(uint32_t) + ((t->array + t->dict + 3) & ~3));
	int i;
	for (i=0;i<t->array;i++) {
		pushvalue(L, v++, t->type[i], doc);
		lua_rawseti(L, tbl, i+1);
	}
	for (i=0;i<t->dict;i++) {
		pushvalue(L, v++, VALUE_STRING, doc);
		pushvalue(L, v++, t->type[t->array+i], doc);
		lua_rawset(L, tbl);
	}
}

static int
lnew(lua_State *L) {
	luaL_checktype(L, 1, LUA_TLIGHTUSERDATA);
	const char * data = lua_touserdata(L, 1);
	// hold ref to data
	lua_getfield(L, LUA_REGISTRYINDEX, TABLES);
	lua_pushvalue(L, 1);
	lua_rawsetp(L, -2, data);

	create_proxy(L, data, 0);
	return 1;
}

static void
copyfromdata(lua_State *L) {
	lua_getfield(L, LUA_REGISTRYINDEX, PROXYCACHE);
	lua_pushvalue(L, 1);
	// PROXYCACHE, table
	if (lua_rawget(L, -2) != LUA_TUSERDATA) {
		luaL_error(L, "Invalid proxy table %p", lua_topointer(L, 1));
	}
	struct proxy * p = lua_touserdata(L, -1);
	lua_pop(L, 2);
	copytable(L, 1, p);
	lua_pushnil(L);
	lua_setmetatable(L, 1);	// remove metatable
}

static int
lindex(lua_State *L) {
	copyfromdata(L);
	lua_rawget(L, 1);
	return 1;
}

static int
lnext(lua_State *L) {
	luaL_checktype(L, 1, LUA_TTABLE);
	lua_settop(L, 2);  /* create a 2nd argument if there isn't one */
	if (lua_next(L, 1))
		return 2;
	else {
		lua_pushnil(L);
		return 1;
	}
}

static int
lpairs(lua_State *L) {
	copyfromdata(L);
	lua_pushcfunction(L, lnext);
	lua_pushvalue(L, 1);
	lua_pushnil(L);
	return 3;
}

static int
llen(lua_State *L) {
	copyfromdata(L);
	lua_pushinteger(L, lua_rawlen(L, 1));
	return 1;
}

static void
new_weak_table(lua_State *L, const char *mode) {
	lua_newtable(L);	// NODECACHE { pointer:table }

	lua_createtable(L, 0, 1);	// weak meta table
	lua_pushstring(L, mode);
	lua_setfield(L, -2, "__mode");

	lua_setmetatable(L, -2);	// make NODECACHE weak
}

static void
gen_metatable(lua_State *L) {
	new_weak_table(L, "kv");	// NODECACHE { pointer:table }
	lua_setfield(L, LUA_REGISTRYINDEX, NODECACHE);

	new_weak_table(L, "k");	// PROXYCACHE { table:userdata }
	lua_setfield(L, LUA_REGISTRYINDEX, PROXYCACHE);

	lua_newtable(L);
	lua_setfield(L, LUA_REGISTRYINDEX, TABLES);

	lua_createtable(L, 0, 1);	// mod table

	lua_createtable(L, 0, 2);	// metatable
	luaL_Reg l[] = {
		{ "__index", lindex },
		{ "__pairs", lpairs },
		{ "__len", llen },
		{ NULL, NULL },
	};
	lua_pushvalue(L, -1);
	luaL_setfuncs(L, l, 1);
}

static int
lstringpointer(lua_State *L) {
	const char * str = luaL_checkstring(L, 1);
	lua_pushlightuserdata(L, (void *)str);
	return 1;
}

LUAMOD_API int
luaopen_skynet_datasheet_core(lua_State *L) {
	luaL_checkversion(L);
	luaL_Reg l[] = {
		{ "new", lnew },
		{ "update", lupdate },
		{ NULL, NULL },
	};

	luaL_newlibtable(L,l);
	gen_metatable(L);
	luaL_setfuncs(L, l, 1);
	lua_pushcfunction(L, lstringpointer);
	lua_setfield(L, -2, "stringpointer");
	return 1;
}
