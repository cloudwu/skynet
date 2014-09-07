/*
** $Id: lpprint.h,v 1.1 2013/03/21 20:25:12 roberto Exp $
*/


#if !defined(lpprint_h)
#define lpprint_h


#include "lptree.h"
#include "lpvm.h"


#if defined(LPEG_DEBUG)

void printpatt (Instruction *p, int n);
void printtree (TTree *tree, int ident);
void printktable (lua_State *L, int idx);
void printcharset (const byte *st);
void printcaplist (Capture *cap, Capture *limit);

#else

#define printktable(L,idx)  \
	luaL_error(L, "function only implemented in debug mode")
#define printtree(tree,i)  \
	luaL_error(L, "function only implemented in debug mode")
#define printpatt(p,n)  \
	luaL_error(L, "function only implemented in debug mode")

#endif


#endif

