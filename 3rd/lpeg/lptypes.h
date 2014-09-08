/*
** $Id: lptypes.h,v 1.8 2013/04/12 16:26:38 roberto Exp $
** LPeg - PEG pattern matching for Lua
** Copyright 2007, Lua.org & PUC-Rio  (see 'lpeg.html' for license)
** written by Roberto Ierusalimschy
*/

#if !defined(lptypes_h)
#define lptypes_h


#if !defined(LPEG_DEBUG)
#define NDEBUG
#endif

#include <assert.h>
#include <limits.h>

#include "lua.h"


#define VERSION         "0.12"


#define PATTERN_T	"lpeg-pattern"
#define MAXSTACKIDX	"lpeg-maxstack"


/*
** compatibility with Lua 5.2
*/
#if (LUA_VERSION_NUM == 502)

#undef lua_equal
#define lua_equal(L,idx1,idx2)  lua_compare(L,(idx1),(idx2),LUA_OPEQ)

#undef lua_getfenv
#define lua_getfenv	lua_getuservalue
#undef lua_setfenv
#define lua_setfenv	lua_setuservalue

#undef lua_objlen
#define lua_objlen	lua_rawlen

#undef luaL_register
#define luaL_register(L,n,f) \
	{ if ((n) == NULL) luaL_setfuncs(L,f,0); else luaL_newlib(L,f); }

#endif


/* default maximum size for call/backtrack stack */
#if !defined(MAXBACK)
#define MAXBACK         100
#endif


/* maximum number of rules in a grammar */
#define MAXRULES        200



/* initial size for capture's list */
#define INITCAPSIZE	32


/* index, on Lua stack, for subject */
#define SUBJIDX		2

/* number of fixed arguments to 'match' (before capture arguments) */
#define FIXEDARGS	3

/* index, on Lua stack, for capture list */
#define caplistidx(ptop)	((ptop) + 2)

/* index, on Lua stack, for pattern's ktable */
#define ktableidx(ptop)		((ptop) + 3)

/* index, on Lua stack, for backtracking stack */
#define stackidx(ptop)	((ptop) + 4)



typedef unsigned char byte;


#define BITSPERCHAR		8

#define CHARSETSIZE		((UCHAR_MAX/BITSPERCHAR) + 1)



typedef struct Charset {
  byte cs[CHARSETSIZE];
} Charset;



#define loopset(v,b)    { int v; for (v = 0; v < CHARSETSIZE; v++) {b;} }

/* access to charset */
#define treebuffer(t)      ((byte *)((t) + 1))

/* number of slots needed for 'n' bytes */
#define bytes2slots(n)  (((n) - 1) / sizeof(TTree) + 1)

/* set 'b' bit in charset 'cs' */
#define setchar(cs,b)   ((cs)[(b) >> 3] |= (1 << ((b) & 7)))


/*
** in capture instructions, 'kind' of capture and its offset are
** packed in field 'aux', 4 bits for each
*/
#define getkind(op)		((op)->i.aux & 0xF)
#define getoff(op)		(((op)->i.aux >> 4) & 0xF)
#define joinkindoff(k,o)	((k) | ((o) << 4))

#define MAXOFF		0xF
#define MAXAUX		0xFF


/* maximum number of bytes to look behind */
#define MAXBEHIND	MAXAUX


/* maximum size (in elements) for a pattern */
#define MAXPATTSIZE	(SHRT_MAX - 10)


/* size (in elements) for an instruction plus extra l bytes */
#define instsize(l)  (((l) + sizeof(Instruction) - 1)/sizeof(Instruction) + 1)


/* size (in elements) for a ISet instruction */
#define CHARSETINSTSIZE		instsize(CHARSETSIZE)

/* size (in elements) for a IFunc instruction */
#define funcinstsize(p)		((p)->i.aux + 2)



#define testchar(st,c)	(((int)(st)[((c) >> 3)] & (1 << ((c) & 7))))


#endif

