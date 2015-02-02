/*
** $Id: lpcode.h,v 1.6 2013/11/28 14:56:02 roberto Exp $
*/

#if !defined(lpcode_h)
#define lpcode_h

#include "lua.h"

#include "lptypes.h"
#include "lptree.h"
#include "lpvm.h"

int tocharset (TTree *tree, Charset *cs);
int checkaux (TTree *tree, int pred);
int fixedlenx (TTree *tree, int count, int len);
int hascaptures (TTree *tree);
int lp_gc (lua_State *L);
Instruction *compile (lua_State *L, Pattern *p);
void realloccode (lua_State *L, Pattern *p, int nsize);
int sizei (const Instruction *i);


#define PEnullable      0
#define PEnofail        1

#define nofail(t)	checkaux(t, PEnofail)
#define nullable(t)	checkaux(t, PEnullable)

#define fixedlen(t)     fixedlenx(t, 0, 0)



#endif
