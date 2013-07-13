#ifndef LUA_COMPAT_52_H
#define LUA_COMPAT_52_H

#include "lua.h"
#include "lauxlib.h"

LUALIB_API void (luaL_init)(lua_State *L);

#if (LUA_VERSION_NUM == 501)

#include <stddef.h>

#define LUA_OK 0
#define LUA_ERRGCMM	6

/* predefined values in the registry */
#define LUA_RIDX_MAINTHREAD	1
#define LUA_RIDX_GLOBALS	2
#define LUA_RIDX_LAST		LUA_RIDX_GLOBALS

#define LUA_UNSIGNED	unsigned int
typedef LUA_UNSIGNED lua_Unsigned;

LUA_API int   (lua_absindex) (lua_State *L, int idx);

#define LUA_OPEQ	0
#define LUA_OPLT	1
#define LUA_OPLE	2

LUA_API int   (lua_compare) (lua_State *L, int idx1, int idx2, int op);

LUA_API void  (lua_copy) (lua_State *L, int fromidx, int toidx);

LUA_API void  (lua_getuservalue) (lua_State *L, int idx);
LUA_API void  (lua_setuservalue) (lua_State *L, int idx);

LUA_API void  (lua_len)    (lua_State *L, int idx);

#define lua_rawlen(L,idx) lua_objlen(L,idx)

LUA_API void  (lua_rawgetp) (lua_State *L, int idx, const void *p);
LUA_API void  (lua_rawsetp) (lua_State *L, int idx, const void *p);

#define lua_tounsigned(L,i)	lua_tounsignedx(L,i,NULL)
LUA_API lua_Unsigned    (lua_tounsignedx) (lua_State *L, int idx, int *isnum);

LUA_API lua_Integer     (lua_tointegerx) (lua_State *L, int idx, int *isnum);
LUA_API lua_Number (lua_tonumberx) (lua_State *L, int index, int *isnum);

LUA_API const lua_Number *(lua_version) (lua_State *L);

#define lua_pushglobaltable(L)  lua_pushvalue(L, LUA_GLOBALSINDEX)

LUALIB_API lua_Unsigned (luaL_checkunsigned) (lua_State *L, int numArg);

LUALIB_API void (luaL_checkversion_) (lua_State *L, lua_Number ver);
#define luaL_checkversion(L)	luaL_checkversion_(L, LUA_VERSION_NUM)

LUALIB_API int (luaL_getsubtable) (lua_State *L, int idx, const char *fname);
LUALIB_API int (luaL_len) (lua_State *L, int idx);

#define luaL_newlibtable(L,l)	\
  lua_createtable(L, 0, sizeof(l)/sizeof((l)[0]) - 1)

#define luaL_newlib(L,l)	(luaL_newlibtable(L,l), luaL_setfuncs(L,l,0))

#define luaL_opt(L,f,n,d)	(lua_isnoneornil(L,(n)) ? (d) : f(L,(n)))

LUALIB_API lua_Unsigned (luaL_optunsigned) (lua_State *L, int numArg,
                                            lua_Unsigned def);

LUALIB_API void (luaL_pushresultsize) (luaL_Buffer *B, size_t sz);

LUALIB_API void (luaL_requiref) (lua_State *L, const char *modname,
                                 lua_CFunction openf, int glb);

LUALIB_API void (luaL_setfuncs) (lua_State *L, const luaL_Reg *l, int nup);

LUALIB_API void  (luaL_setmetatable) (lua_State *L, const char *tname);

LUALIB_API const char *(luaL_tolstring) (lua_State *L, int idx, size_t *len);

#endif

#endif
