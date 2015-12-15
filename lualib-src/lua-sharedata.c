#include <lua.h>
#include <lauxlib.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "atomic.h"

#define KEYTYPE_INTEGER 0
#define KEYTYPE_STRING 1

#define VALUETYPE_NIL 0
#define VALUETYPE_REAL 1
#define VALUETYPE_STRING 2
#define VALUETYPE_BOOLEAN 3
#define VALUETYPE_TABLE 4
#define VALUETYPE_INTEGER 5

struct table;

union value {
	lua_Number n;
	lua_Integer d;
	struct table * tbl;
	int string;
	int boolean;
};

struct node {
	union value v;
	int key;	// integer key or index of string table
	int next;	// next slot index
	uint32_t keyhash;
	uint8_t keytype;	// key type must be integer or string
	uint8_t valuetype;	// value type can be number/string/boolean/table
	uint8_t nocolliding;	// 0 means colliding slot
};

struct state {
	int dirty;
	int ref;
	struct table * root;
};

struct table {
	int sizearray;
	int sizehash;
	uint8_t *arraytype;
	union value * array;
	struct node * hash;
	lua_State * L;
};

struct context {
	lua_State * L;
	struct table * tbl;
	int string_index;
};

struct ctrl {
	struct table * root;
	struct table * update;
};

static int
countsize(lua_State *L, int sizearray) {
	int n = 0;
	lua_pushnil(L);
	while (lua_next(L, 1) != 0) {
		int type = lua_type(L, -2);
		++n;
		if (type == LUA_TNUMBER) {
			if (!lua_isinteger(L, -2)) {
				luaL_error(L, "Invalid key %f", lua_tonumber(L, -2));
			}
			lua_Integer nkey = lua_tointeger(L, -2);
			if (nkey > 0 && nkey <= sizearray) {
				--n;
			}
		} else if (type != LUA_TSTRING && type != LUA_TTABLE) {
			luaL_error(L, "Invalid key type %s", lua_typename(L, type));
		}
		lua_pop(L, 1);
	}

	return n;
}

static uint32_t
calchash(const char * str, size_t l) {
	uint32_t h = (uint32_t)l;
	size_t l1;
	size_t step = (l >> 5) + 1;
	for (l1 = l; l1 >= step; l1 -= step) {
		h = h ^ ((h<<5) + (h>>2) + (uint8_t)(str[l1 - 1]));
	}
	return h;
}

static int
stringindex(struct context *ctx, const char * str, size_t sz) {
	lua_State *L = ctx->L;
	lua_pushlstring(L, str, sz);
	lua_pushvalue(L, -1);
	lua_rawget(L, 1);
	int index;
	// stringmap(1) str index
	if (lua_isnil(L, -1)) {
		index = ++ctx->string_index;
		lua_pop(L, 1);
		lua_pushinteger(L, index);
		lua_rawset(L, 1);
	} else {
		index = lua_tointeger(L, -1);
		lua_pop(L, 2);
	}
	return index;
}

static int convtable(lua_State *L);

static void
setvalue(struct context * ctx, lua_State *L, int index, struct node *n) {
	int vt = lua_type(L, index);
	switch(vt) {
	case LUA_TNIL:
		n->valuetype = VALUETYPE_NIL;
		break;
	case LUA_TNUMBER:
		if (lua_isinteger(L, index)) {
			n->v.d = lua_tointeger(L, index);
			n->valuetype = VALUETYPE_INTEGER;
		} else {
			n->v.n = lua_tonumber(L, index);
			n->valuetype = VALUETYPE_REAL;
		}
		break;
	case LUA_TSTRING: {
		size_t sz = 0;
		const char * str = lua_tolstring(L, index, &sz);
		n->v.string = stringindex(ctx, str, sz);
		n->valuetype = VALUETYPE_STRING;
		break;
	}
	case LUA_TBOOLEAN:
		n->v.boolean = lua_toboolean(L, index);
		n->valuetype = VALUETYPE_BOOLEAN;
		break;
	case LUA_TTABLE: {
		struct table *tbl = ctx->tbl;
		ctx->tbl = (struct table *)malloc(sizeof(struct table));
		if (ctx->tbl == NULL) {
			ctx->tbl = tbl;
			luaL_error(L, "memory error");
			// never get here
		}
		memset(ctx->tbl, 0, sizeof(struct table));
		int absidx = lua_absindex(L, index);

		lua_pushcfunction(L, convtable);
		lua_pushvalue(L, absidx);
		lua_pushlightuserdata(L, ctx);

		lua_call(L, 2, 0);

		n->v.tbl = ctx->tbl;
		n->valuetype = VALUETYPE_TABLE;

		ctx->tbl = tbl;

		break;
	}
	default:
		luaL_error(L, "Unsupport value type %s", lua_typename(L, vt));
		break;
	}
}

static void
setarray(struct context *ctx, lua_State *L, int index, int key) {
	struct node n;
	setvalue(ctx, L, index, &n);
	struct table *tbl = ctx->tbl;
	--key;	// base 0
	tbl->arraytype[key] = n.valuetype;
	tbl->array[key] = n.v;
}

static int
ishashkey(struct context * ctx, lua_State *L, int index, int *key, uint32_t *keyhash, int *keytype) {
	int sizearray = ctx->tbl->sizearray;
	int kt = lua_type(L, index);
	if (kt == LUA_TNUMBER) {
		*key = lua_tointeger(L, index);
		if (*key > 0 && *key <= sizearray) {
			return 0;
		}
		*keyhash = (uint32_t)*key;
		*keytype = KEYTYPE_INTEGER;
	} else {
		size_t sz = 0;
		const char * s = lua_tolstring(L, index, &sz);
		*keyhash = calchash(s, sz);
		*key = stringindex(ctx, s, sz);
		*keytype = KEYTYPE_STRING;
	}
	return 1;
}

static void
fillnocolliding(lua_State *L, struct context *ctx) {
	struct table * tbl = ctx->tbl;
	lua_pushnil(L);
	while (lua_next(L, 1) != 0) {
		int key;
		int keytype;
		uint32_t keyhash;
		if (!ishashkey(ctx, L, -2, &key, &keyhash, &keytype)) {
			setarray(ctx, L, -1, key);
		} else {
			struct node * n = &tbl->hash[keyhash % tbl->sizehash];
			if (n->valuetype == VALUETYPE_NIL) {
				n->key = key;
				n->keytype = keytype;
				n->keyhash = keyhash;
				n->next = -1;
				n->nocolliding = 1;
				setvalue(ctx, L, -1, n);	// set n->v , n->valuetype
			}
		}
		lua_pop(L,1);
	}
}

static void
fillcolliding(lua_State *L, struct context *ctx) {
	struct table * tbl = ctx->tbl;
	int sizehash = tbl->sizehash;
	int emptyslot = 0;
	int i;
	lua_pushnil(L);
	while (lua_next(L, 1) != 0) {
		int key;
		int keytype;
		uint32_t keyhash;
		if (ishashkey(ctx, L, -2, &key, &keyhash, &keytype)) {
			struct node * mainpos = &tbl->hash[keyhash % tbl->sizehash];
			if (!(mainpos->keytype == keytype && mainpos->key == key)) {
				// the key has not insert
				struct node * n = NULL;
				for (i=emptyslot;i<sizehash;i++) {
					if (tbl->hash[i].valuetype == VALUETYPE_NIL) {
						n = &tbl->hash[i];
						break;
					}
				}
				assert(n);
				n->next = mainpos->next;
				mainpos->next = n - tbl->hash;
				mainpos->nocolliding = 0;
				n->key = key;
				n->keytype = keytype;
				n->keyhash = keyhash;
				n->nocolliding = 0;
				setvalue(ctx, L, -1, n);	// set n->v , n->valuetype
			}
		}
		lua_pop(L,1);
	}
}

// table need convert
// struct context * ctx
static int
convtable(lua_State *L) {
	int i;
	struct context *ctx = lua_touserdata(L,2);
	struct table *tbl = ctx->tbl;

	tbl->L = ctx->L;

	int sizearray = lua_rawlen(L, 1);
	if (sizearray) {
		tbl->arraytype = (uint8_t *)malloc(sizearray * sizeof(uint8_t));
		if (tbl->arraytype == NULL) {
			goto memerror;
		}
		for (i=0;i<sizearray;i++) {
			tbl->arraytype[i] = VALUETYPE_NIL;
		}
		tbl->array = (union value *)malloc(sizearray * sizeof(union value));
		if (tbl->array == NULL) {
			goto memerror;
		}
		tbl->sizearray = sizearray;
	}
	int sizehash = countsize(L, sizearray);
	if (sizehash) {
		tbl->hash = (struct node *)malloc(sizehash * sizeof(struct node));
		if (tbl->hash == NULL) {
			goto memerror;
		}
		for (i=0;i<sizehash;i++) {
			tbl->hash[i].valuetype = VALUETYPE_NIL;
			tbl->hash[i].nocolliding = 0;
		}
		tbl->sizehash = sizehash;

		fillnocolliding(L, ctx);
		fillcolliding(L, ctx);
	} else {
		int i;
		for (i=1;i<=sizearray;i++) {
			lua_rawgeti(L, 1, i);
			setarray(ctx, L, -1, i);
			lua_pop(L,1);
		}
	}

	return 0;
memerror:
	return luaL_error(L, "memory error");
}

static void
delete_tbl(struct table *tbl) {
	int i;
	for (i=0;i<tbl->sizearray;i++) {
		if (tbl->arraytype[i] == VALUETYPE_TABLE) {
			delete_tbl(tbl->array[i].tbl);
		}
	}
	for (i=0;i<tbl->sizehash;i++) {
		if (tbl->hash[i].valuetype == VALUETYPE_TABLE) {
			delete_tbl(tbl->hash[i].v.tbl);
		}
	}
	free(tbl->arraytype);
	free(tbl->array);
	free(tbl->hash);
	free(tbl);
}

static int
pconv(lua_State *L) {
	struct context *ctx = lua_touserdata(L,1);
	lua_State * pL = lua_touserdata(L, 2);
	int ret;

	lua_settop(L, 0);

	// init L (may throw memory error)
	// create a table for string map
	lua_newtable(L);

	lua_pushcfunction(pL, convtable);
	lua_pushvalue(pL,1);
	lua_pushlightuserdata(pL, ctx);

	ret = lua_pcall(pL, 2, 0, 0);
	if (ret != LUA_OK) {
		size_t sz = 0;
		const char * error = lua_tolstring(pL, -1, &sz);
		lua_pushlstring(L, error, sz);
		lua_error(L);
		// never get here
	}

	luaL_checkstack(L, ctx->string_index + 3, NULL);
	lua_settop(L,1);

	return 1;
}

static void
convert_stringmap(struct context *ctx, struct table *tbl) {
	lua_State *L = ctx->L;
	lua_checkstack(L, ctx->string_index + LUA_MINSTACK);
	lua_settop(L, ctx->string_index + 1);
	lua_pushvalue(L, 1);
	struct state * s = lua_newuserdata(L, sizeof(*s));
	s->dirty = 0;
	s->ref = 0;
	s->root = tbl;
	lua_replace(L, 1);
	lua_replace(L, -2);

	lua_pushnil(L);
	// ... stringmap nil
	while (lua_next(L, -2) != 0) {
		int idx = lua_tointeger(L, -1);
		lua_pop(L, 1);
		lua_pushvalue(L, -1);
		lua_replace(L, idx);
	}

	lua_pop(L, 1);

	lua_gc(L, LUA_GCCOLLECT, 0);
}

static int
lnewconf(lua_State *L) {
	int ret;
	struct context ctx;
	struct table * tbl = NULL;
	luaL_checktype(L,1,LUA_TTABLE);
	ctx.L = luaL_newstate();
	ctx.tbl = NULL;
	ctx.string_index = 1;	// 1 reserved for dirty flag
	if (ctx.L == NULL) {
		lua_pushliteral(L, "memory error");
		goto error;
	}
	tbl = (struct table *)malloc(sizeof(struct table));
	if (tbl == NULL) {
		// lua_pushliteral may fail because of memory error, close first.
		lua_close(ctx.L);
		ctx.L = NULL;
		lua_pushliteral(L, "memory error");
		goto error;
	}
	memset(tbl, 0, sizeof(struct table));
	ctx.tbl = tbl;

	lua_pushcfunction(ctx.L, pconv);
	lua_pushlightuserdata(ctx.L , &ctx);
	lua_pushlightuserdata(ctx.L , L);

	ret = lua_pcall(ctx.L, 2, 1, 0);

	if (ret != LUA_OK) {
		size_t sz = 0;
		const char * error = lua_tolstring(ctx.L, -1, &sz);
		lua_pushlstring(L, error, sz);
		goto error;
	}

	convert_stringmap(&ctx, tbl);

	lua_pushlightuserdata(L, tbl);	

	return 1;
error:
	if (ctx.L) {
		lua_close(ctx.L);
	}
	if (tbl) {
		delete_tbl(tbl);
	}
	lua_error(L);
	return -1;
}

static struct table *
get_table(lua_State *L, int index) {
	struct table *tbl = lua_touserdata(L,index);
	if (tbl == NULL) {
		luaL_error(L, "Need a conf object");
	}
	return tbl;
}

static int
ldeleteconf(lua_State *L) {
	struct table *tbl = get_table(L,1);
	lua_close(tbl->L);
	delete_tbl(tbl);
	return 0;
}

static void
pushvalue(lua_State *L, lua_State *sL, uint8_t vt, union value *v) {
	switch(vt) {
	case VALUETYPE_REAL:
		lua_pushnumber(L, v->n);
		break;
	case VALUETYPE_INTEGER:
		lua_pushinteger(L, v->d);
		break;
	case VALUETYPE_STRING: {
		size_t sz = 0;
		const char *str = lua_tolstring(sL, v->string, &sz);
		lua_pushlstring(L, str, sz);
		break;
	}
	case VALUETYPE_BOOLEAN:
		lua_pushboolean(L, v->boolean);
		break;
	case VALUETYPE_TABLE:
		lua_pushlightuserdata(L, v->tbl);
		break;
	default:
		lua_pushnil(L);
		break;
	}
}

static struct node *
lookup_key(struct table *tbl, uint32_t keyhash, int key, int keytype, const char *str, size_t sz) {
	if (tbl->sizehash == 0)
		return NULL;
	struct node *n = &tbl->hash[keyhash % tbl->sizehash];
	if (keyhash != n->keyhash && n->nocolliding)
		return NULL;
	for (;;) {
		if (keyhash == n->keyhash) {
			if (n->keytype == KEYTYPE_INTEGER) {
				if (keytype == KEYTYPE_INTEGER && n->key == key) {
					return n;
				}
			} else {
				// n->keytype == KEYTYPE_STRING
				if (keytype == KEYTYPE_STRING) {
					size_t sz2 = 0;
					const char * str2 = lua_tolstring(tbl->L, n->key, &sz2);
					if (sz == sz2 && memcmp(str,str2,sz) == 0) {
						return n;
					}
				}
			}
		}
		if (n->next < 0) {
			return NULL;
		}
		n = &tbl->hash[n->next];		
	}
}

static int
lindexconf(lua_State *L) {
	struct table *tbl = get_table(L,1);
	int kt = lua_type(L,2);
	uint32_t keyhash;
	int key = 0;
	int keytype;
	size_t sz = 0;
	const char * str = NULL;
	if (kt == LUA_TNUMBER) {
		if (!lua_isinteger(L, 2)) {
			return luaL_error(L, "Invalid key %f", lua_tonumber(L, 2));
		}
		key = (int)lua_tointeger(L, 2);
		if (key > 0 && key <= tbl->sizearray) {
			--key;
			pushvalue(L, tbl->L, tbl->arraytype[key], &tbl->array[key]);
			return 1;
		}
		keytype = KEYTYPE_INTEGER;
		keyhash = (uint32_t)key;
	} else {
		str = luaL_checklstring(L, 2, &sz);
		keyhash = calchash(str, sz);
		keytype = KEYTYPE_STRING;
	}

	struct node *n = lookup_key(tbl, keyhash, key, keytype, str, sz);
	if (n) {
		pushvalue(L, tbl->L, n->valuetype, &n->v);
		return 1;
	} else {
		return 0;
	}
}

static void
pushkey(lua_State *L, lua_State *sL, struct node *n) {
	if (n->keytype == KEYTYPE_INTEGER) {
		lua_pushinteger(L, n->key);
	} else {
		size_t sz = 0;
		const char * str = lua_tolstring(sL, n->key, &sz);
		lua_pushlstring(L, str, sz);
	}
}

static int
pushfirsthash(lua_State *L, struct table * tbl) {
	if (tbl->sizehash) {
		pushkey(L, tbl->L, &tbl->hash[0]);
		return 1;
	} else {
		return 0;
	}
}

static int
lnextkey(lua_State *L) {
	struct table *tbl = get_table(L,1);
	if (lua_isnoneornil(L,2)) {
		if (tbl->sizearray > 0) {
			int i;
			for (i=0;i<tbl->sizearray;i++) {
				if (tbl->arraytype[i] != VALUETYPE_NIL) {
					lua_pushinteger(L, i+1);
					return 1;
				}
			}
		}
		return pushfirsthash(L, tbl);
	}
	int kt = lua_type(L,2);
	uint32_t keyhash;
	int key = 0;
	int keytype;
	size_t sz=0;
	const char *str = NULL;
	int sizearray = tbl->sizearray;
	if (kt == LUA_TNUMBER) {
		if (!lua_isinteger(L, 2)) {
			return 0;
		}
		key = (int)lua_tointeger(L, 2);
		if (key > 0 && key <= sizearray) {
			lua_Integer i;
			for (i=key;i<sizearray;i++) {
				if (tbl->arraytype[i] != VALUETYPE_NIL) {
					lua_pushinteger(L, i+1);
					return 1;
				}
			}
			return pushfirsthash(L, tbl);
		}
		keyhash = (uint32_t)key;
		keytype = KEYTYPE_INTEGER;
	} else {
		str = luaL_checklstring(L, 2, &sz);
		keyhash = calchash(str, sz);
		keytype = KEYTYPE_STRING;
	}

	struct node *n = lookup_key(tbl, keyhash, key, keytype, str, sz);
	if (n) {
		++n;
		int index = n-tbl->hash;
		if (index == tbl->sizehash) {
			return 0;
		}
		pushkey(L, tbl->L, n);
		return 1;
	} else {
		return 0;
	}
}

static int
llen(lua_State *L) {
	struct table *tbl = get_table(L,1);
	lua_pushinteger(L, tbl->sizearray);
	return 1;
}

static int
lhashlen(lua_State *L) {
	struct table *tbl = get_table(L,1);
	lua_pushinteger(L, tbl->sizehash);
	return 1;
}

static int
releaseobj(lua_State *L) {
	struct ctrl *c = lua_touserdata(L, 1);
	struct table *tbl = c->root;
	struct state *s = lua_touserdata(tbl->L, 1);
	ATOM_DEC(&s->ref);
	c->root = NULL;
	c->update = NULL;

	return 0;
}

static int
lboxconf(lua_State *L) {
	struct table * tbl = get_table(L,1);	
	struct state * s = lua_touserdata(tbl->L, 1);
	ATOM_INC(&s->ref);

	struct ctrl * c = lua_newuserdata(L, sizeof(*c));
	c->root = tbl;
	c->update = NULL;
	if (luaL_newmetatable(L, "confctrl")) {
		lua_pushcfunction(L, releaseobj);
		lua_setfield(L, -2, "__gc");
	}
	lua_setmetatable(L, -2);

	return 1;
}

static int
lmarkdirty(lua_State *L) {
	struct table *tbl = get_table(L,1);
	struct state * s = lua_touserdata(tbl->L, 1);
	s->dirty = 1;
	return 0;
}

static int
lisdirty(lua_State *L) {
	struct table *tbl = get_table(L,1);
	struct state * s = lua_touserdata(tbl->L, 1);
	int d = s->dirty;
	lua_pushboolean(L, d);
	
	return 1;
}

static int
lgetref(lua_State *L) {
	struct table *tbl = get_table(L,1);
	struct state * s = lua_touserdata(tbl->L, 1);
	lua_pushinteger(L , s->ref);

	return 1;
}

static int
lincref(lua_State *L) {
	struct table *tbl = get_table(L,1);
	struct state * s = lua_touserdata(tbl->L, 1);
	int ref = ATOM_INC(&s->ref);
	lua_pushinteger(L , ref);

	return 1;
}

static int
ldecref(lua_State *L) {
	struct table *tbl = get_table(L,1);
	struct state * s = lua_touserdata(tbl->L, 1);
	int ref = ATOM_DEC(&s->ref);
	lua_pushinteger(L , ref);

	return 1;
}

static int
lneedupdate(lua_State *L) {
	struct ctrl * c = lua_touserdata(L, 1);
	if (c->update) {
		lua_pushlightuserdata(L, c->update);
		lua_getuservalue(L, 1);
		return 2;
	}
	return 0;
}

static int
lupdate(lua_State *L) {
	luaL_checktype(L, 1, LUA_TUSERDATA);
	luaL_checktype(L, 2, LUA_TLIGHTUSERDATA);
	luaL_checktype(L, 3, LUA_TTABLE);
	struct ctrl * c= lua_touserdata(L, 1);
	struct table *n = lua_touserdata(L, 2);
	if (c->root == n) {
		return luaL_error(L, "You should update a new object");
	}
	lua_settop(L, 3);
	lua_setuservalue(L, 1);
	c->update = n;

	return 0;
}

int
luaopen_sharedata_core(lua_State *L) {
	luaL_Reg l[] = {
		// used by host
		{ "new", lnewconf },
		{ "delete", ldeleteconf },
		{ "markdirty", lmarkdirty },
		{ "getref", lgetref },
		{ "incref", lincref },
		{ "decref", ldecref },

		// used by client
		{ "box", lboxconf },
		{ "index", lindexconf },
		{ "nextkey", lnextkey },
		{ "len", llen },
		{ "hashlen", lhashlen },
		{ "isdirty", lisdirty },
		{ "needupdate", lneedupdate },
		{ "update", lupdate },
		{ NULL, NULL },
	};
	luaL_checkversion(L);
	luaL_newlib(L, l);

	return 1;
}
