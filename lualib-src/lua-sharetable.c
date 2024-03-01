#define LUA_LIB

#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
#include <string.h>

#include "lgc.h"
#include "ltable.h"

#include "atomic.h"

#ifdef makeshared

static void
mark_shared(lua_State *L) {
	if (lua_type(L, -1) != LUA_TTABLE) {
		luaL_error(L, "Not a table, it's a %s.", lua_typename(L, lua_type(L, -1)));
	}
	Table * t = (Table *)lua_topointer(L, -1);
	if (isshared(t))
		return;
	makeshared(t);
	luaL_checkstack(L, 4, NULL);
	if (lua_getmetatable(L, -1)) {
		luaL_error(L, "Can't share metatable");
	}
	lua_pushnil(L);
	while (lua_next(L, -2) != 0) {
		int i;
		for (i=0;i<2;i++) {
			int idx = -i-1;
			int t = lua_type(L, idx);
			switch (t) {
			case LUA_TTABLE:
				mark_shared(L);
				break;
			case LUA_TNUMBER:
			case LUA_TBOOLEAN:
			case LUA_TLIGHTUSERDATA:
				break;
			case LUA_TFUNCTION:
				if (lua_getupvalue(L, idx, 1) != NULL) {
					luaL_error(L, "Invalid function with upvalue");
				} else if (!lua_iscfunction(L, idx)) {
					LClosure *f = (LClosure *)lua_topointer(L, idx);
					makeshared(f);
					lua_sharefunction(L, idx);
				}
				break;
			case LUA_TSTRING:
				lua_sharestring(L, idx);
				break;
			default:
				luaL_error(L, "Invalid type [%s]", lua_typename(L, t));
				break;
			}
		}
		lua_pop(L, 1);
	}
}

static int
lis_sharedtable(lua_State* L) {
	int b = 0;
	if(lua_type(L, 1) == LUA_TTABLE) {
		Table * t = (Table *)lua_topointer(L, 1);
		b = isshared(t);
	}
	lua_pushboolean(L, b);
	return 1;
}

static int
make_matrix(lua_State *L) {
	// turn off gc , because marking shared will prevent gc mark.
	lua_gc(L, LUA_GCSTOP, 0);
	mark_shared(L);
	Table * t = (Table *)lua_topointer(L, -1);
	lua_pushlightuserdata(L, t);
	return 1;
}

static int
clone_table(lua_State *L) {
	lua_clonetable(L, lua_touserdata(L, 1));

	return 1;
}

static int
lco_stackvalues(lua_State* L) {
    lua_State *cL = lua_tothread(L, 1);
    luaL_argcheck(L, cL, 1, "thread expected");
    int n = 0;
    if(cL != L) {
        luaL_checktype(L, 2, LUA_TTABLE);
        n = lua_gettop(cL);
        if(n > 0) {
            luaL_checkstack(L, n+1, NULL);
            int top = lua_gettop(L);
            lua_xmove(cL, L, n);
            int i=0;
            for(i=1; i<=n; i++) {
                lua_pushvalue(L, top+i);
                lua_seti(L, 2, i);
            }
            lua_xmove(L, cL, n);
        }
    }

    lua_pushinteger(L, n);
    return 1;
}


struct state_ud {
	lua_State *L;
};

struct thread_counter {
	ATOM_INT count;
};

static int
close_state(lua_State *L) {
	struct state_ud *ud = (struct state_ud *)luaL_checkudata(L, 1, "BOXMATRIXSTATE");
	if (ud->L) {
		lua_close(ud->L);
		ud->L = NULL;
	}
	return 0;
}

static int
get_matrix(lua_State *L) {
	struct state_ud *ud = (struct state_ud *)luaL_checkudata(L, 1, "BOXMATRIXSTATE");
	if (ud->L) {
		const void * v = lua_topointer(ud->L, 1);
		lua_pushlightuserdata(L, (void *)v);
		return 1;
	}
	return 0;
}

static int
get_size(lua_State *L) {
	struct state_ud *ud = (struct state_ud *)luaL_checkudata(L, 1, "BOXMATRIXSTATE");
	if (ud->L) {
		lua_Integer sz = lua_gc(ud->L, LUA_GCCOUNT, 0);
		sz *= 1024;
		sz += lua_gc(ud->L, LUA_GCCOUNTB, 0);
		lua_pushinteger(L, sz);
	} else {
		lua_pushinteger(L, 0);
	}
	return 1;
}

static
int iter_aux (lua_State *L) {
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
pairs_iter(lua_State *L) {
    luaL_checktype(L, 1, LUA_TTABLE);
    lua_settop(L, 2);  /* create a 2nd argument if there isn't one */
    lua_pushcfunction(L, iter_aux);
    lua_pushvalue(L, 1);
    lua_pushvalue(L, 2);
    int ok = lua_pcall(L, 2, 2, 0);
    if (ok != LUA_OK || lua_type(L, -1) == LUA_TNIL) {
        if (luaL_getmetafield(L, 1, "__index") == LUA_TNIL) {  /* no metamethod? */
            if (ok != LUA_OK) {
                lua_pop(L, 1); // pop errormsg
		        luaL_error(L, "invalid key to 'next'");
            } else {
                return 1;
            }
        } else {
            lua_pushcfunction(L, iter_aux); // iterate __index table
            lua_insert(L, -2);
            if ( ok != LUA_OK)
                lua_pushvalue(L, 2); // push key
            else
                lua_pushnil(L);
            lua_call(L, 2, 2);
            return 2;
        }
    }
    return 2;
}

static int
pairs_func(lua_State *L) {
    lua_pushcfunction(L, pairs_iter);
    lua_pushvalue(L, 1);
    lua_pushnil(L);
    return 3;
}

static int
len_func(lua_State *L) {
    lua_Integer len = (lua_Integer)lua_rawlen(L, 1);
    if (luaL_getmetafield(L, 1, "__index") == LUA_TNIL) {  /* no metamethod? */
        lua_pushinteger(L, len);
    } else {
        lua_Integer i = len + 1;
        lua_Integer sum_len = len;
        for (;;i++) {
            if (lua_geti(L, -1, i) == LUA_TNIL) {
                break;
            }
            lua_pop(L, 1);
            sum_len = i;
        }
        lua_pushinteger(L, sum_len);
    }
    return 1;
}

static void
convert_pairs(lua_State *L) {
	if (lua_type(L, -1) != LUA_TTABLE) {
		luaL_error(L, "Not a table, it's a %s.", lua_typename(L, lua_type(L, -1)));
	}
    lua_pushnil(L);
    while (lua_next(L, -2) != 0) {
        int tk = lua_type(L, -2);
        if (tk == LUA_TSTRING) {
            const char *key = lua_tostring(L, -2);
            if (strcmp(key, "__pairs") == 0) {
                lua_pushcfunction(L, pairs_func);
		        lua_pushvalue(L, -1);
                lua_copy(L, -4, -2);
                lua_settable(L, -5);
            } else if (strcmp(key, "__len") == 0) {
                lua_pushcfunction(L, len_func);
		        lua_pushvalue(L, -1);
                lua_copy(L, -4, -2);
                lua_settable(L, -5);
            }
        }
        int tv = lua_type(L, -1);
        if ( tv == LUA_TTABLE)
            convert_pairs(L);
        lua_pop(L, 1);
    }
}

static int
inc_update(lua_State *L) {
    struct state_ud *ud = (struct state_ud *)luaL_checkudata(L, 1, "BOXMATRIXSTATE");
    if (ud->L) {
	    lua_checkstack(ud->L, lua_tointeger(L, 3));
        lua_pushcfunction(ud->L, lua_tocfunction(L, 2));
        lua_pushlightuserdata(ud->L, lua_touserdata(L, 4));
        lua_pushinteger(ud->L, lua_tointeger(L, 5));
        if (lua_pcall(ud->L, 2, 1, 0) != LUA_OK)
            lua_error(ud->L);
        convert_pairs(ud->L);
        //lua_gc(ud->L, LUA_GCCOLLECT, 0);
        lua_pushcfunction(ud->L, make_matrix);
        lua_insert(ud->L, -2);
        lua_call(ud->L, 1, 1);
		const void * v = lua_topointer(ud->L, -1);
		lua_pushlightuserdata(L, (void *)v);
        return 1;
    }
    return 0;
}

static int
get_update(lua_State *L) {
    struct state_ud *ud = (struct state_ud *)luaL_checkudata(L, 1, "BOXMATRIXSTATE");
    if (ud->L) {
		const void * v = lua_topointer(ud->L, -1);
		lua_pushlightuserdata(L, (void *)v);
        return 1;
    }
    return 0;
}

static int
new_thread_counter(lua_State *L) {
    struct state_ud *ud = (struct state_ud *)luaL_checkudata(L, 1, "BOXMATRIXSTATE");
    if (ud->L) {
	    struct thread_counter *c = (struct thread_counter *)lua_newuserdatauv(ud->L, sizeof(*c), 0);
        lua_setfield(L, LUA_REGISTRYINDEX, "SHARETABLE_THREADCOUNTER");
        c->count = 0;
		lua_pushlightuserdata(L, (void *)c);
        return 1;
    }
    return 0;
}

static int
box_state(lua_State *L, lua_State *mL) {
	struct state_ud *ud = (struct state_ud *)lua_newuserdatauv(L, sizeof(*ud), 0);
	ud->L = mL;
	if (luaL_newmetatable(L, "BOXMATRIXSTATE")) {
		lua_pushvalue(L, -1);
		lua_setfield(L, -2, "__index");
		lua_pushcfunction(L, close_state);
		lua_setfield(L, -2, "close");
		lua_pushcfunction(L, get_matrix);
		lua_setfield(L, -2, "getptr");
		lua_pushcfunction(L, get_size);
		lua_setfield(L, -2, "size");
		lua_pushcfunction(L, inc_update);
		lua_setfield(L, -2, "inc_update");
		lua_pushcfunction(L, get_update);
		lua_setfield(L, -2, "get_update");
		lua_pushcfunction(L, new_thread_counter);
		lua_setfield(L, -2, "new_thread_counter");
	}
	lua_setmetatable(L, -2);

	return 1;
}

static int
load_matrixfile(lua_State *L) {
	luaL_openlibs(L);
	const char * source = (const char *)lua_touserdata(L, 1);
	if (source[0] == '@') {
		if (luaL_loadfilex_(L, source+1, NULL) != LUA_OK)
			lua_error(L);
	} else {
		if (luaL_loadstring(L, source) != LUA_OK)
			lua_error(L);
	}
	lua_replace(L, 1);
	if (lua_pcall(L, lua_gettop(L) - 1, 1, 0) != LUA_OK)
		lua_error(L);
	lua_gc(L, LUA_GCCOLLECT, 0);
	lua_pushcfunction(L, make_matrix);
	lua_insert(L, -2);
	lua_call(L, 1, 1);
	return 1;
}

static int
matrix_from_file(lua_State *L) {
	lua_State *mL = luaL_newstate();
	if (mL == NULL) {
		return luaL_error(L, "luaL_newstate failed");
	}
	const char * source = luaL_checkstring(L, 1);
	int top = lua_gettop(L);
	lua_pushcfunction(mL, load_matrixfile);
	lua_pushlightuserdata(mL, (void *)source);
	if (top > 1) {
		if (!lua_checkstack(mL, top + 1)) {
			return luaL_error(L, "Too many argument %d", top);
		}
		int i;
		for (i=2;i<=top;i++) {
			switch(lua_type(L, i)) {
			case LUA_TBOOLEAN:
				lua_pushboolean(mL, lua_toboolean(L, i));
				break;
			case LUA_TNUMBER:
				if (lua_isinteger(L, i)) {
					lua_pushinteger(mL, lua_tointeger(L, i));
				} else {
					lua_pushnumber(mL, lua_tonumber(L, i));
				}
				break;
			case LUA_TLIGHTUSERDATA:
				lua_pushlightuserdata(mL, lua_touserdata(L, i));
				break;
			case LUA_TFUNCTION:
				if (lua_iscfunction(L, i) && lua_getupvalue(L, i, 1) == NULL) {
					lua_pushcfunction(mL, lua_tocfunction(L, i));
					break;
				}
				return luaL_argerror(L, i, "Only support light C function");
			default:
				return luaL_argerror(L, i, "Type invalid");
			}
		}
	}
	int ok = lua_pcall(mL, top, 1, 0);
	if (ok != LUA_OK) {
		lua_pushstring(L, lua_tostring(mL, -1));
		lua_close(mL);
		lua_error(L);
	}
	return box_state(L, mL);
}

static int
lset_sharedtable(lua_State *L) {
	Table *t = (Table *)lua_topointer(L, 1);
    TValue *key = s2v(L->top.p - 2);
    TValue *v = s2v(L->top.p - 1);
    TValue *slot = (TValue *)luaH_get(t, key);
    if (ttype(v) != ttype(slot)) // nil value only distinguish by tt_
        setnilvalue(slot);
    setobj2t(L,slot, v);
    return 0;
}

static int
linc_thread_cnt(lua_State *L) {
	struct thread_counter *counter_ptr = (struct thread_counter *)lua_touserdata(L, 1);
	ATOM_FINC(&counter_ptr->count);
    return 0;
}

static int
lget_thread_cnt(lua_State *L) {
	struct thread_counter *counter_ptr = (struct thread_counter *)lua_touserdata(L, 1);
    lua_pushinteger(L, ATOM_LOAD(&counter_ptr->count));
    return 1;
}

static int
lreset_thread_cnt(lua_State *L) {
	struct thread_counter *counter_ptr = (struct thread_counter *)lua_touserdata(L, 1);
    ATOM_FSUB(&counter_ptr->count, ATOM_LOAD(&counter_ptr->count));
    return 0;
}

static int
lset_metatable(lua_State *L) {
	Table *t = (Table *)lua_topointer(L, 1); // raw table
	Table *mt = (Table *)lua_topointer(L, 2); // metatable
    t->metatable = mt;
    return 0;
}

LUAMOD_API int
luaopen_skynet_sharetable_core(lua_State *L) {
	luaL_checkversion(L);
	luaL_Reg l[] = {
		{ "clone", clone_table },
		{ "stackvalues", lco_stackvalues }, 
		{ "matrix", matrix_from_file },
		{ "is_sharedtable", lis_sharedtable },
		{ "set_table", lset_sharedtable },
		{ "inc_thread_cnt", linc_thread_cnt },
		{ "get_thread_cnt", lget_thread_cnt },
		{ "reset_thread_cnt", lreset_thread_cnt },
		{ "set_metatable", lset_metatable },
		{ NULL, NULL },
	};
	luaL_newlib(L, l);
	return 1;
}

#else

LUAMOD_API int
luaopen_skynet_sharetable_core(lua_State *L) {
	return luaL_error(L, "No share string table support");
}

#endif
