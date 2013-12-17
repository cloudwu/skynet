#include "luacode_cache.h"

#include <lua.h>
#include <lauxlib.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#define LOCK(q) while (__sync_lock_test_and_set(&(q)->lock,1)) {}
#define UNLOCK(q) __sync_lock_release(&(q)->lock);

struct codecache {
	int lock;
	lua_State *L;
};

static struct codecache CC = { 0 , NULL };

static void 
_clearcache() {
	if (CC.L == NULL)
		return;
	LOCK(&CC)
		lua_close(CC.L);
		CC.L = luaL_newstate();
	UNLOCK(&CC)
}

static void
_init() {
	CC.lock = 0;
	CC.L = luaL_newstate();
}

static const char *
_load(const char *key, size_t *sz) {
	if (CC.L == NULL)
		return NULL;
	LOCK(&CC)
		lua_State *L = CC.L;
		lua_getglobal(L, key);
		const char * result = NULL;
		if (lua_isstring(L,-1)) {
			result = lua_tolstring(L, -1, sz);
		}
		lua_pop(L, 1);
	UNLOCK(&CC)

	return result;
}

static const char *
_save(const char *key, const char * code, size_t *sz) {
	lua_State *L;
	const char * result = NULL;

	LOCK(&CC)
		if (CC.L == NULL) {
			_init();
			L = CC.L;
		} else {
			L = CC.L;
			lua_getglobal(L, key);
			if (lua_isstring(L,-1)) {
				size_t code_sz = *sz;
				result = lua_tolstring(L, -1, sz);
				assert(code_sz == *sz);
				lua_pop(L, 1);
				goto _ret;
			}
			lua_pop(L, 1);
		}
		lua_pushlstring(L, code, *sz);
		result = lua_tostring(L, -1);
		lua_setglobal(L, key);
_ret:
	UNLOCK(&CC)
	return result;
}

const char * 
luacode_load(const char * key, const char * code, size_t *sz) {
	if (code == NULL) {
		return _load(key, sz);
	}
	const char * result = _load(key,sz);
	if (result) {
		return result;
	}
	return _save(key, code, sz);
}

// copy from lbaselib.c
static int 
load_aux (lua_State *L, int status, int envidx) {
	if (status == LUA_OK) {
		if (envidx != 0) {  /* 'env' parameter? */
			lua_pushvalue(L, envidx);  /* environment for loaded function */
			if (!lua_setupvalue(L, -2, 1))  /* set it as 1st upvalue */
				lua_pop(L, 1);  /* remove 'env' if not used by previous call */
		}
		return 1;
	}
	else {  /* error (message is on top of the stack) */
		lua_pushnil(L);
		lua_insert(L, -2);  /* put before error message */
		return 2;  /* return nil plus error message */
	}
}

static int 
writer (lua_State *L, const void* b, size_t size, void* B) {
	(void)L;
	luaL_addlstring((luaL_Buffer*) B, (const char *)b, size);
	return 0;
}

static void
str_dump (lua_State *L) {
	luaL_Buffer b;
	luaL_buffinit(L,&b);
	if (lua_dump(L, writer, &b) != 0)
		luaL_error(L, "unable to dump given function");
	luaL_pushresult(&b);
}


static int
cache_loadfile(lua_State *L) {
	const char *fname = luaL_optstring(L, 1, NULL);
	const char *mode = luaL_optstring(L, 2, NULL);
	int env = (!lua_isnone(L, 3) ? 3 : 0);  /* 'env' index or 0 if no 'env' */
	int status;
	if (fname == NULL) {
		status = luaL_loadfilex(L, fname, mode);
	} else {
		// read cache
		size_t sz = 0;
		const char * bytecode = luacode_load(fname, NULL, &sz);
		if (bytecode) {
			// load from cache
			status = luaL_loadbuffer(L, bytecode, sz, fname);
		} else {
			status = luaL_loadfilex(L, fname, mode);
			if (status == LUA_OK) {
				str_dump(L);
				const char * bytecode = lua_tolstring(L,-1,&sz);
				// update cache
				luacode_load(fname, bytecode, &sz);
				lua_pop(L,1);
			}
		}
	}
	return load_aux(L, status, env);
}

/*
** reserved slot, above all arguments, to hold a copy of the returned
** string to avoid it being collected while parsed. 'load' has four
** optional arguments (chunk, source name, mode, and environment).
*/
#define RESERVEDSLOT	5

/*
** Reader for generic `load' function: `lua_load' uses the
** stack for internal stuff, so the reader cannot change the
** stack top. Instead, it keeps its resulting string in a
** reserved slot inside the stack.
*/
static const char *generic_reader (lua_State *L, void *ud, size_t *size) {
  (void)(ud);  /* not used */
  luaL_checkstack(L, 2, "too many nested functions");
  lua_pushvalue(L, 1);  /* get function */
  lua_call(L, 0, 1);  /* call it */
  if (lua_isnil(L, -1)) {
    lua_pop(L, 1);  /* pop result */
    *size = 0;
    return NULL;
  }
  else if (!lua_isstring(L, -1))
    luaL_error(L, "reader function must return a string");
  lua_replace(L, RESERVEDSLOT);  /* save string in reserved slot */
  return lua_tolstring(L, RESERVEDSLOT, size);
}

static int 
cache_load(lua_State *L) {
	int status;
	size_t l;
	const char *s = lua_tolstring(L, 1, &l);
	const char *mode = luaL_optstring(L, 3, "bt");
	int env = (!lua_isnone(L, 4) ? 4 : 0);  /* 'env' index or 0 if no 'env' */
	const char *cname = lua_tostring(L,2);
	if (cname) {
		// read cache
		size_t sz = 0;
		const char * bytecode = luacode_load(cname, NULL, &sz);
		if (bytecode) {
			// load from cache
			status = luaL_loadbuffer(L, bytecode, sz, cname);
			return load_aux(L, status, env);
		}
	}

	if (s != NULL) {  /* loading a string? */
		const char *chunkname = luaL_optstring(L, 2, s);
		status = luaL_loadbufferx(L, s, l, chunkname, mode);
	}
	else {  /* loading from a reader function */
		const char *chunkname = luaL_optstring(L, 2, "=(load)");
		luaL_checktype(L, 1, LUA_TFUNCTION);
		lua_settop(L, RESERVEDSLOT);  /* create reserved slot */
		status = lua_load(L, generic_reader, NULL, chunkname, mode);
	}
	if (cname && status == LUA_OK) {
		size_t sz = 0;
		str_dump(L);
		const char * bytecode = lua_tolstring(L,-1,&sz);
		// update cache
		luacode_load(cname, bytecode, &sz);
		lua_pop(L,1);
	}

	return load_aux(L, status, env);
}

int 
luacode_loadfile(lua_State *L, const char *filename) {
	size_t sz = 0;
	const char * bytecode = luacode_load(filename, NULL, &sz);
	int status;
	if (bytecode) {
		status = luaL_loadbuffer(L, bytecode, sz, filename);
	} else {
		status = luaL_loadfile(L, filename);
		if (status == LUA_OK) {
			str_dump(L);
			const char * bytecode = lua_tolstring(L,-1,&sz);
			// update cache
			luacode_load(filename, bytecode, &sz);
			lua_pop(L,1);
		}
	}
	return status;
}

// require loader, copy and modify from loadlib.c

#if !defined (LUA_PATH_SEP)
#define LUA_PATH_SEP		";"
#endif
#if !defined (LUA_PATH_MARK)
#define LUA_PATH_MARK		"?"
#endif
#if !defined(LUA_LSUBSEP)
#define LUA_LSUBSEP		LUA_DIRSEP
#endif

static int 
readable (const char *filename) {
	size_t sz = 0;
	const char * bytecode = luacode_load(filename, NULL, &sz);
	if (bytecode) {
		return 1;
	}
	FILE *f = fopen(filename, "r");  /* try to open file */
	if (f == NULL) return 0;  /* open failed */
	fclose(f);
	return 1;
}

static const char *
pushnexttemplate (lua_State *L, const char *path) {
	const char *l;
	while (*path == *LUA_PATH_SEP) path++;  /* skip separators */
	if (*path == '\0') return NULL;  /* no more templates */
	l = strchr(path, *LUA_PATH_SEP);  /* find next separator */
	if (l == NULL) l = path + strlen(path);
	lua_pushlstring(L, path, l - path);  /* template */
	return l;
}


static const char *
searchpath (lua_State *L, const char *name,
			 const char *path,
			 const char *sep,
			 const char *dirsep) {
	luaL_Buffer msg;  /* to build error message */
	luaL_buffinit(L, &msg);
	if (*sep != '\0')  /* non-empty separator? */
		name = luaL_gsub(L, name, sep, dirsep);  /* replace it by 'dirsep' */
	while ((path = pushnexttemplate(L, path)) != NULL) {
		const char *filename = luaL_gsub(L, lua_tostring(L, -1),
										 LUA_PATH_MARK, name);
		lua_remove(L, -2);  /* remove path template */
		if (readable(filename))  /* does file exist and is readable? */
			return filename;  /* return that file name */
		lua_pushfstring(L, "\n\tno file " LUA_QS, filename);
		lua_remove(L, -2);  /* remove file name */
		luaL_addvalue(&msg);  /* concatenate error msg. entry */
	}
	luaL_pushresult(&msg);  /* create error message */
	return NULL;  /* not found */
}

static const char *
findfile (lua_State *L, const char *name,
					 const char *pname,
					 const char *dirsep) {
	const char *path;
	lua_getfield(L, lua_upvalueindex(1), pname);
	path = lua_tostring(L, -1);
	if (path == NULL)
		luaL_error(L, LUA_QL("package.%s") " must be a string", pname);
	return searchpath(L, name, path, ".", dirsep);
}

static int 
checkload (lua_State *L, int stat, const char *filename) {
	if (stat) {  /* module loaded successfully? */
		lua_pushstring(L, filename);  /* will be 2nd argument to module */
		return 2;  /* return open function and file name */
	}
	else
		return luaL_error(L, "error loading module " LUA_QS
						 " from file " LUA_QS ":\n\t%s",
						  lua_tostring(L, 1), filename, lua_tostring(L, -1));
}

static int 
searcher_cache (lua_State *L) {
	const char *filename;
	const char *name = luaL_checkstring(L, 1);
	filename = findfile(L, name, "path", LUA_LSUBSEP);
	if (filename == NULL) return 1;  /* module not found in this path */
	return checkload(L, (luacode_loadfile(L, filename) == LUA_OK), filename);
}

static int
replace_searcher_lua(lua_State *L) {
	lua_getglobal(L, "package");
	if (!lua_istable(L,-1)) {
//		luaL_error(L, "Can't find package");
		return 1;
	}
	lua_getfield(L, -1, "searchers");
	if (!lua_istable(L,-1)) {
//		luaL_error(L, "Can't find package.searchers");
		return 1;
	}
	lua_pushvalue(L,-2);
	lua_pushcclosure(L, searcher_cache, 1);
	// package.searcher[2] is searcher_lua, replace it.
	lua_rawseti(L, -2, 2);
	lua_pop(L,2);

	return 0;
}

static int
cache_clear(lua_State *L) {
	_clearcache();
	return 0;
}

//-------------------------

int 
luacode_lib(lua_State *L) {
	if (replace_searcher_lua(L)) {
		// Don't support package.searchers
		return 0;
	}
	luaL_Reg l[] = {
		{ "load", cache_load },
		{ "clear", cache_clear },
		{ "loadfile", cache_loadfile },
		{ NULL, NULL },
	};
	luaL_newlib(L,l);
	return 1;
}


