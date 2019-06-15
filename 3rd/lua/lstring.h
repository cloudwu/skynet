/*
** $Id: lstring.h,v 1.61.1.1 2017/04/19 17:20:42 roberto Exp $
** String table (keep all strings handled by Lua)
** See Copyright Notice in lua.h
*/

#ifndef lstring_h
#define lstring_h

#include "lgc.h"
#include "lobject.h"
#include "lstate.h"


#define sizelstring(l)  (sizeof(union UTString) + ((l) + 1) * sizeof(char))

#define sizeludata(l)	(sizeof(union UUdata) + (l))
#define sizeudata(u)	sizeludata((u)->len)

#define luaS_newliteral(L, s)	(luaS_newlstr(L, "" s, \
                                 (sizeof(s)/sizeof(char))-1))


/*
** test whether a string is a reserved word
*/
#define isreserved(s)	((s)->tt == LUA_TSHRSTR && (s)->extra > 0)


/*
** equality for short strings, which are always internalized
*/
#define eqshrstr(a,b)	check_exp((a)->tt == LUA_TSHRSTR, (a) == (b))


LUAI_FUNC unsigned int luaS_hash (const char *str, size_t l, unsigned int seed);
LUAI_FUNC unsigned int luaS_hashlongstr (TString *ts);
LUAI_FUNC int luaS_eqlngstr (TString *a, TString *b);
LUAI_FUNC void luaS_clearcache (global_State *g);
LUAI_FUNC void luaS_init (lua_State *L);
LUAI_FUNC Udata *luaS_newudata (lua_State *L, size_t s);
LUAI_FUNC TString *luaS_newlstr (lua_State *L, const char *str, size_t l);
LUAI_FUNC TString *luaS_new (lua_State *L, const char *str);
LUAI_FUNC TString *luaS_createlngstrobj (lua_State *L, size_t l);

#define ENABLE_SHORT_STRING_TABLE

struct ssm_info {
	int total;
	int longest;
	int slots;
	int garbage;
	size_t size;
	size_t garbage_size;
	double variance;
};

struct ssm_collect {
	void *key;
	int n;
	int sweep;
};

LUA_API void luaS_initssm();
LUA_API void luaS_exitssm();
LUA_API void luaS_infossm(struct ssm_info *info);
LUA_API int luaS_collectssm(struct ssm_collect *info);

LUAI_FUNC void luaS_mark(global_State *g, TString *s);
LUAI_FUNC void luaS_fix(global_State *g, TString *s);
LUAI_FUNC void luaS_collect(global_State *g, int closed);

#endif
