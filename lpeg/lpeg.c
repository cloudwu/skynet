/*
** $Id: lpeg.c,v 1.114 2011/02/16 15:02:20 roberto Exp $
** LPeg - PEG pattern matching for Lua
** Copyright 2007, Lua.org & PUC-Rio  (see 'lpeg.html' for license)
** written by Roberto Ierusalimschy
*/


#include <assert.h>
#include <ctype.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

#include "lua.h"
#include "lauxlib.h"

#include "lpeg.h"


#define VERSION		"0.10"
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



/* initial size for call/backtrack stack */
#define INITBACK	100

/* default maximum size for call/backtrack stack */
#define MAXBACK		INITBACK

/* size for call/backtrack stack for verifier */
#define MAXBACKVER	200

/* initial size for capture's list */
#define INITCAPSIZE	32


/* index, on Lua stack, for subject */
#define SUBJIDX		2

/* number of fixed arguments to 'match' (before capture arguments) */
#define FIXEDARGS	3

/* index, on Lua stack, for substitution value cache */
#define subscache(cs)	((cs)->ptop + 1)

/* index, on Lua stack, for capture list */
#define caplistidx(ptop)	((ptop) + 2)

/* index, on Lua stack, for pattern's fenv */
#define penvidx(ptop)	((ptop) + 3)

/* index, on Lua stack, for backtracking stack */
#define stackidx(ptop)	((ptop) + 4)



typedef unsigned char byte;


#define CHARSETSIZE		((UCHAR_MAX/CHAR_BIT) + 1)


typedef byte Charset[CHARSETSIZE];


/* Virtual Machine's instructions */
typedef enum Opcode {
  IAny, IChar, ISet, ISpan,
  IBack,
  IRet, IEnd,
  IChoice, IJmp, ICall, IOpenCall,
  ICommit, IPartialCommit, IBackCommit, IFailTwice, IFail, IGiveup,
  IFunc,
  IFullCapture, IEmptyCapture, IEmptyCaptureIdx,
  IOpenCapture, ICloseCapture, ICloseRunTime
} Opcode;


#define ISJMP		0x1
#define ISCHECK		0x2
#define ISFIXCHECK	0x4
#define ISNOFAIL	0x8
#define ISCAPTURE	0x10
#define ISMOVABLE	0x20
#define ISFENVOFF	0x40

static const int opproperties[] = {
  /* IAny */		ISCHECK | ISFIXCHECK | ISJMP,
  /* IChar */		ISCHECK | ISFIXCHECK | ISJMP,
  /* ISet */		ISCHECK | ISFIXCHECK | ISJMP,
  /* ISpan */		ISNOFAIL,
  /* IBack */		0,
  /* IRet */		0,
  /* IEnd */		0,
  /* IChoice */		ISJMP,
  /* IJmp */		ISJMP | ISNOFAIL,
  /* ICall */		ISJMP,
  /* IOpenCall */	ISFENVOFF,
  /* ICommit */		ISJMP,
  /* IPartialCommit */	ISJMP,
  /* IBackCommit */	ISJMP,
  /* IFailTwice */	0,
  /* IFail */		0,
  /* IGiveup */		0,
  /* IFunc */		ISCHECK | ISJMP,
  /* IFullCapture */	ISCAPTURE | ISNOFAIL | ISFENVOFF,
  /* IEmptyCapture */	ISCAPTURE | ISNOFAIL | ISMOVABLE,
  /* IEmptyCaptureIdx */ISCAPTURE | ISNOFAIL | ISMOVABLE | ISFENVOFF,
  /* IOpenCapture */	ISCAPTURE | ISNOFAIL | ISMOVABLE | ISFENVOFF,
  /* ICloseCapture */	ISCAPTURE | ISNOFAIL | ISMOVABLE | ISFENVOFF,
  /* ICloseRunTime */	ISCAPTURE | ISFENVOFF
};


typedef union Instruction {
  struct Inst {
    byte code;
    byte aux;
    short offset;
  } i;
  PattFunc f;
  int iv;
  byte buff[1];
} Instruction;

static const Instruction giveup = {{IGiveup, 0, 0}};

#define getkind(op)	((op)->i.aux & 0xF)
#define getoff(op)	(((op)->i.aux >> 4) & 0xF)

#define dest(p,x)	((x) + ((p)+(x))->i.offset)

#define MAXOFF		0xF
#define MAXAUX		0xFF

/* maximum size (in elements) for a pattern */
#define MAXPATTSIZE	(SHRT_MAX - 10)


#define isprop(op,p)	(opproperties[(op)->i.code] & (p))
#define isjmp(op)	(isprop(op, ISJMP) && (op)->i.offset != 0)
#define iscapture(op) 	isprop(op, ISCAPTURE)
#define ischeck(op)	(isprop(op, ISCHECK) && (op)->i.offset == 0)
#define isfixcheck(op)	(isprop(op, ISFIXCHECK) && (op)->i.offset == 0)
#define istest(op)	(isprop(op, ISCHECK) && (op)->i.offset != 0)
#define isnofail(op)	isprop(op, ISNOFAIL)
#define ismovable(op)	isprop(op, ISMOVABLE)
#define isfenvoff(op)	isprop(op, ISFENVOFF)


/* kinds of captures */
typedef enum CapKind {
  Cclose, Cposition, Cconst, Cbackref, Carg, Csimple, Ctable, Cfunction,
  Cquery, Cstring, Csubst, Cfold, Cruntime, Cgroup
} CapKind;

#define iscapnosize(k)	((k) == Cposition || (k) == Cconst)


typedef struct Capture {
  const char *s;  /* position */
  short idx;
  byte kind;
  byte siz;
} Capture;


/* size (in elements) for an instruction plus extra l bytes */
#define instsize(l)  (((l) + sizeof(Instruction) - 1)/sizeof(Instruction) + 1)


/* size (in elements) for a ISet instruction */
#define CHARSETINSTSIZE		instsize(CHARSETSIZE)

/* size (in elements) for a IFunc instruction */
#define funcinstsize(p)		((p)->i.aux + 2)


#define loopset(v,b)	{ int v; for (v = 0; v < CHARSETSIZE; v++) b; }


#define testchar(st,c)	(((int)(st)[((c) >> 3)] & (1 << ((c) & 7))))
#define setchar(st,c)	((st)[(c) >> 3] |= (1 << ((c) & 7)))


static int target (Instruction *p, int i);


static int sizei (const Instruction *i) {
  switch((Opcode)i->i.code) {
    case ISet: case ISpan: return CHARSETINSTSIZE;
    case IFunc: return funcinstsize(i);
    default: return 1;
  }
}


static const char *val2str (lua_State *L, int idx) {
  const char *k = lua_tostring(L, idx);
  if (k != NULL)
    return lua_pushfstring(L, "rule '%s'", k);
  else
    return lua_pushfstring(L, "rule <a %s>", luaL_typename(L, idx));
}


static int getposition (lua_State *L, int t, int i) {
  int res;
  lua_getfenv(L, -1);
  lua_rawgeti(L, -1, i);  /* get key from pattern's environment */
  lua_gettable(L, t);  /* get position from positions table */
  res = lua_tointeger(L, -1);
  if (res == 0) {  /* key has no registered position? */
    lua_rawgeti(L, -2, i);  /* get key again */
    return luaL_error(L, "%s is not defined in given grammar", val2str(L, -1));
  }
  lua_pop(L, 2);  /* remove environment and position */
  return res;
}


/*
** {======================================================
** Printing patterns (for debugging)
** =======================================================
*/


static void printcharset (const Charset st) {
  int i;
  printf("[");
  for (i = 0; i <= UCHAR_MAX; i++) {
    int first = i;
    while (testchar(st, i) && i <= UCHAR_MAX) i++;
    if (i - 1 == first)  /* unary range? */
      printf("(%02x)", first);
    else if (i - 1 > first)  /* non-empty range? */
      printf("(%02x-%02x)", first, i - 1);
  }
  printf("]");
}


static void printcapkind (int kind) {
  const char *const modes[] = {
    "close", "position", "constant", "backref",
    "argument", "simple", "table", "function",
    "query", "string", "substitution", "fold",
    "runtime", "group"};
  printf("%s", modes[kind]);
}


static void printjmp (const Instruction *op, const Instruction *p) {
  printf("-> ");
  if (p->i.offset == 0) printf("FAIL");
  else printf("%d", (int)(dest(0, p) - op));
}


static void printinst (const Instruction *op, const Instruction *p) {
  const char *const names[] = {
    "any", "char", "set", "span", "back",
    "ret", "end",
    "choice", "jmp", "call", "open_call",
    "commit", "partial_commit", "back_commit", "failtwice", "fail", "giveup",
     "func",
     "fullcapture", "emptycapture", "emptycaptureidx", "opencapture",
     "closecapture", "closeruntime"
  };
  printf("%02ld: %s ", (long)(p - op), names[p->i.code]);
  switch ((Opcode)p->i.code) {
    case IChar: {
      printf("'%c'", p->i.aux);
      printjmp(op, p);
      break;
    }
    case IAny: {
      printf("* %d", p->i.aux);
      printjmp(op, p);
      break;
    }
    case IFullCapture: case IOpenCapture:
    case IEmptyCapture: case IEmptyCaptureIdx:
    case ICloseCapture: case ICloseRunTime: {
      printcapkind(getkind(p));
      printf("(n = %d)  (off = %d)", getoff(p), p->i.offset);
      break;
    }
    case ISet: {
      printcharset((p+1)->buff);
      printjmp(op, p);
      break;
    }
    case ISpan: {
      printcharset((p+1)->buff);
      break;
    }
    case IOpenCall: {
      printf("-> %d", p->i.offset);
      break;
    }
    case IChoice: {
      printjmp(op, p);
      printf(" (%d)", p->i.aux);
      break;
    }
    case IJmp: case ICall: case ICommit:
    case IPartialCommit: case IBackCommit: {
      printjmp(op, p);
      break;
    }
    default: break;
  }
  printf("\n");
}


static void printpatt (Instruction *p) {
  Instruction *op = p;
  for (;;) {
    printinst(op, p);
    if ((Opcode)p->i.code == IEnd) break;
    p += sizei(p);
  }
}


#if 0
static void printcap (Capture *cap) {
  printcapkind(cap->kind);
  printf(" (idx: %d - size: %d) -> %p\n", cap->idx, cap->siz, cap->s);
}


static void printcaplist (Capture *cap) {
  for (; cap->s; cap++) printcap(cap);
}
#endif

/* }====================================================== */


/*
** {======================================================
** Virtual Machine
** =======================================================
*/


typedef struct Stack {
  const char *s;
  const Instruction *p;
  int caplevel;
} Stack;


#define getstackbase(L, ptop)	((Stack *)lua_touserdata(L, stackidx(ptop)))


static int runtimecap (lua_State *L, Capture *close, Capture *ocap,
                       const char *o, const char *s, int ptop);


static Capture *doublecap (lua_State *L, Capture *cap, int captop, int ptop) {
  Capture *newc;
  if (captop >= INT_MAX/((int)sizeof(Capture) * 2))
    luaL_error(L, "too many captures");
  newc = (Capture *)lua_newuserdata(L, captop * 2 * sizeof(Capture));
  memcpy(newc, cap, captop * sizeof(Capture));
  lua_replace(L, caplistidx(ptop));
  return newc;
}


static Stack *doublestack (lua_State *L, Stack **stacklimit, int ptop) {
  Stack *stack = getstackbase(L, ptop);
  Stack *newstack;
  int n = *stacklimit - stack;
  int max, newn;
  lua_getfield(L, LUA_REGISTRYINDEX, MAXSTACKIDX);
  max = lua_tointeger(L, -1);
  lua_pop(L, 1);
  if (n >= max)
    luaL_error(L, "too many pending calls/choices");
  newn = 2*n; if (newn > max) newn = max;
  newstack = (Stack *)lua_newuserdata(L, newn * sizeof(Stack));
  memcpy(newstack, stack, n * sizeof(Stack));
  lua_replace(L, stackidx(ptop));
  *stacklimit = newstack + newn;
  return newstack + n;

}


static void adddyncaptures (const char *s, Capture *base, int n, int fd) {
  int i;
  assert(base[0].kind == Cruntime && base[0].siz == 0);
  base[0].idx = fd;  /* first returned capture */
  for (i = 1; i < n; i++) {  /* add extra captures */
    base[i].siz = 1;  /* mark it as closed */
    base[i].s = s;
    base[i].kind = Cruntime;
    base[i].idx = fd + i;  /* stack index */
  }
  base[n].kind = Cclose;  /* add closing entry */
  base[n].siz = 1;
  base[n].s = s;
}


#define condfailed(p)	{ int f = p->i.offset; if (f) p+=f; else goto fail; }

static const char *match (lua_State *L,
                          const char *o, const char *s, const char *e,
                          Instruction *op, Capture *capture, int ptop) {
  Stack stackbase[INITBACK];
  Stack *stacklimit = stackbase + INITBACK;
  Stack *stack = stackbase;  /* point to first empty slot in stack */
  int capsize = INITCAPSIZE;
  int captop = 0;  /* point to first empty slot in captures */
  const Instruction *p = op;
  stack->p = &giveup; stack->s = s; stack->caplevel = 0; stack++;
  lua_pushlightuserdata(L, stackbase);
  for (;;) {
#if defined(DEBUG)
      printf("s: |%s| stck: %d c: %d  ",
             s, stack - getstackbase(L, ptop), captop);
      printinst(op, p);
#endif
    switch ((Opcode)p->i.code) {
      case IEnd: {
        assert(stack == getstackbase(L, ptop) + 1);
        capture[captop].kind = Cclose;
        capture[captop].s = NULL;
        return s;
      }
      case IGiveup: {
        assert(stack == getstackbase(L, ptop));
        return NULL;
      }
      case IRet: {
        assert(stack > getstackbase(L, ptop) && (stack - 1)->s == NULL);
        p = (--stack)->p;
        continue;
      }
      case IAny: {
        int n = p->i.aux;
        if (n <= e - s) { p++; s += n; }
        else condfailed(p);
        continue;
      }
      case IChar: {
        if ((byte)*s == p->i.aux && s < e) { p++; s++; }
        else condfailed(p);
        continue;
      }
      case ISet: {
        int c = (byte)*s;
        if (testchar((p+1)->buff, c) && s < e)
          { p += CHARSETINSTSIZE; s++; }
        else condfailed(p);
        continue;
      }
      case IBack: {
        int n = p->i.aux;
        if (n > s - o) goto fail;
        s -= n; p++;
        continue;
      }
      case ISpan: {
        for (; s < e; s++) {
          int c = (byte)*s;
          if (!testchar((p+1)->buff, c)) break;
        }
        p += CHARSETINSTSIZE;
        continue;
      }
      case IFunc: {
        const char *r = (p+1)->f(s, e, o, (p+2)->buff);
        if (r != NULL) { s = r; p += funcinstsize(p); }
        else condfailed(p);
        continue;
      }
      case IJmp: {
        p += p->i.offset;
        continue;
      }
      case IChoice: {
        if (stack == stacklimit)
          stack = doublestack(L, &stacklimit, ptop);
        stack->p = dest(0, p);
        stack->s = s - p->i.aux;
        stack->caplevel = captop;
        stack++;
        p++;
        continue;
      }
      case ICall: {
        if (stack == stacklimit)
          stack = doublestack(L, &stacklimit, ptop);
        stack->s = NULL;
        stack->p = p + 1;  /* save return address */
        stack++;
        p += p->i.offset;
        continue;
      }
      case ICommit: {
        assert(stack > getstackbase(L, ptop) && (stack - 1)->s != NULL);
        stack--;
        p += p->i.offset;
        continue;
      }
      case IPartialCommit: {
        assert(stack > getstackbase(L, ptop) && (stack - 1)->s != NULL);
        (stack - 1)->s = s;
        (stack - 1)->caplevel = captop;
        p += p->i.offset;
        continue;
      }
      case IBackCommit: {
        assert(stack > getstackbase(L, ptop) && (stack - 1)->s != NULL);
        s = (--stack)->s;
        captop = stack->caplevel;
        p += p->i.offset;
        continue;
      }
      case IFailTwice:
        assert(stack > getstackbase(L, ptop));
        stack--;
        /* go through */
      case IFail:
      fail: { /* pattern failed: try to backtrack */
        do {  /* remove pending calls */
          assert(stack > getstackbase(L, ptop));
          s = (--stack)->s;
        } while (s == NULL);
        captop = stack->caplevel;
        p = stack->p;
        continue;
      }
      case ICloseRunTime: {
        int fr = lua_gettop(L) + 1;  /* stack index of first result */
        int ncap = runtimecap(L, capture + captop, capture, o, s, ptop);
        lua_Integer res = lua_tointeger(L, fr) - 1;  /* offset */
        int n = lua_gettop(L) - fr;  /* number of new captures */
        if (res == -1) {  /* may not be a number */
          if (!lua_toboolean(L, fr)) {  /* false value? */
            lua_settop(L, fr - 1);  /* remove results */
            goto fail;  /* and fail */
          }
          else if (lua_isboolean(L, fr))  /* true? */
            res = s - o;  /* keep current position */
        }
        if (res < s - o || res > e - o)
          luaL_error(L, "invalid position returned by match-time capture");
        s = o + res;  /* update current position */
        captop -= ncap;  /* remove nested captures */
        lua_remove(L, fr);  /* remove first result (offset) */
        if (n > 0) {  /* captures? */
          if ((captop += n + 1) >= capsize) {
            capture = doublecap(L, capture, captop, ptop);
            capsize = 2 * captop;
          }
          adddyncaptures(s, capture + captop - n - 1, n, fr);
        }
        p++;
        continue;
      }
      case ICloseCapture: {
        const char *s1 = s - getoff(p);
        assert(captop > 0);
        if (capture[captop - 1].siz == 0 &&
            s1 - capture[captop - 1].s < UCHAR_MAX) {
          capture[captop - 1].siz = s1 - capture[captop - 1].s + 1;
          p++;
          continue;
        }
        else {
          capture[captop].siz = 1;  /* mark entry as closed */
          goto capture;
        }
      }
      case IEmptyCapture: case IEmptyCaptureIdx:
        capture[captop].siz = 1;  /* mark entry as closed */
        goto capture;
      case IOpenCapture:
        capture[captop].siz = 0;  /* mark entry as open */
        goto capture;
      case IFullCapture:
        capture[captop].siz = getoff(p) + 1;  /* save capture size */
      capture: {
        capture[captop].s = s - getoff(p);
        capture[captop].idx = p->i.offset;
        capture[captop].kind = getkind(p);
        if (++captop >= capsize) {
          capture = doublecap(L, capture, captop, ptop);
          capsize = 2 * captop;
        }
        p++;
        continue;
      }
      case IOpenCall: {
        lua_rawgeti(L, penvidx(ptop), p->i.offset);
        luaL_error(L, "reference to %s outside a grammar", val2str(L, -1));
      }
      default: assert(0); return NULL;
    }
  }
}

/* }====================================================== */


/*
** {======================================================
** Verifier
** =======================================================
*/


/*
** check whether pattern may go from 'p' to 'e' without consuming any
** input. Raises an error if it detects a left recursion. 'op' points
** the beginning of the pattern.  If pattern belongs to a grammar,
** 'rule' is the stack index where is its corresponding key (only for
** error messages) and 'posttable' is the stack index with a table
** mapping rule keys to the position of their code in the pattern.
*/
static int verify (lua_State *L, Instruction *op, const Instruction *p,
                   Instruction *e, int postable, int rule) {
  static const char dummy[] = "";
  Stack back[MAXBACKVER];
  int backtop = 0;  /* point to first empty slot in back */
  while (p != e) {
    switch ((Opcode)p->i.code) {
      case IRet: {
        p = back[--backtop].p;
        continue;
      }
      case IChoice: {
        if (backtop >= MAXBACKVER)
          return luaL_error(L, "too many pending calls/choices");
        back[backtop].p = dest(0, p);
        back[backtop++].s = dummy;
        p++;
        continue;
      }
      case ICall: {
        assert((p + 1)->i.code != IRet);  /* no tail call */
        if (backtop >= MAXBACKVER)
          return luaL_error(L, "too many pending calls/choices");
        back[backtop].s = NULL;
        back[backtop++].p = p + 1;
        goto dojmp;
      }
      case IOpenCall: {
        int i;
        if (postable == 0)  /* grammar still not fixed? */
          goto fail;  /* to be verified later */
        for (i = 0; i < backtop; i++) {
          if (back[i].s == NULL && back[i].p == p + 1)
            return luaL_error(L, "%s is left recursive", val2str(L, rule));
        }
        if (backtop >= MAXBACKVER)
          return luaL_error(L, "too many pending calls/choices");
        back[backtop].s = NULL;
        back[backtop++].p = p + 1;
        p = op + getposition(L, postable, p->i.offset);
        continue;
      }
      case IBackCommit:
      case ICommit: {
        assert(backtop > 0 && p->i.offset > 0);
        backtop--;
        goto dojmp;
      }
      case IPartialCommit: {
        assert(backtop > 0);
        if (p->i.offset > 0) goto dojmp;  /* forward jump */
        else {  /* loop will be detected when checking corresponding rule */
          assert(postable != 0);
          backtop--;
          p++;  /* just go on now */
          continue;
        }
      }
      case IBack: {
        if (p->i.aux == 1 && isfixcheck(p + 1)) {  /* char test? */
          p++;  /* skip back instruction */
          p += sizei(p);  /* skip char test */
        }
        else {  /* standard lookbehind code */
          assert((Opcode)(p - 1)->i.code == IChoice);  /* look behind */
          backtop--;
          p += (p - 1)->i.offset;
          assert((Opcode)(p - 1)->i.code == IFail);  /* look behind */
        }
        continue;
      }
      case IAny:
      case IChar:
      case ISet: {
        const Instruction *next = p + sizei(p);
        if ((Opcode)next->i.code == IBack)
          p = next + 1;  /* continue after the back instruction */
        else if (p->i.offset == 0) goto fail;
        else  /* jump */
          p += p->i.offset;
        continue;
      }
      case IJmp:
      dojmp: {
        p += p->i.offset;
        continue;
      }
      case IFailTwice:  /* 'not' predicate */
        goto fail;  /* body could have failed; try to backtrack it */
      case IFail: {
        if (p > op && (p - 1)->i.code == IBackCommit) {  /* 'and' predicate? */
          p++;  /* pretend it succeeded and go ahead */
          continue;
        }
        /* else failed: go through */
      }
      fail: { /* pattern failed: try to backtrack */
        do {
          if (backtop-- == 0)
            return 1;  /* no more backtracking */
        } while (back[backtop].s == NULL);
        p = back[backtop].p;
        continue;
      }
      case ISpan:
      case IOpenCapture: case ICloseCapture:
      case IEmptyCapture: case IEmptyCaptureIdx:
      case IFullCapture: {
        p += sizei(p);
        continue;
      }
      case ICloseRunTime: {
        goto fail;  /* be liberal in this case */
      }
      case IFunc: {
        const char *r = (p+1)->f(dummy, dummy, dummy, (p+2)->buff);
        if (r != NULL) { p += funcinstsize(p); }
        else condfailed(p);
        continue;
      }
      case IEnd:  /* cannot happen (should stop before it) */
      default: assert(0); return 0;
    }
  }
  assert(backtop == 0);
  return 0;
}


static void checkrule (lua_State *L, Instruction *op, int from, int to,
                       int postable, int rule) {
  int i;
  int lastopen = 0;  /* more recent OpenCall seen in the code */
  for (i = from; i < to; i += sizei(op + i)) {
    if (op[i].i.code == IPartialCommit && op[i].i.offset < 0) {  /* loop? */
      int start = dest(op, i);
      assert(op[start - 1].i.code == IChoice &&
             dest(op, start - 1) == target(op, i + 1));
      if (start <= lastopen) {  /* loop does contain an open call? */
        if (!verify(L, op, op + start, op + i, postable, rule)) /* check body */
          luaL_error(L, "possible infinite loop in %s", val2str(L, rule));
      }
    }
    else if (op[i].i.code == IOpenCall)
      lastopen = i;
  }
  assert(op[i - 1].i.code == IRet);
  verify(L, op, op + from, op + to - 1, postable, rule);
}




/* }====================================================== */


/*
** {======================================================
** Building Patterns
** =======================================================
*/

enum charsetanswer { NOINFO, ISCHARSET, VALIDSTARTS };

typedef struct CharsetTag {
  enum charsetanswer tag;
  Charset cs;
} CharsetTag;


static Instruction *getpatt (lua_State *L, int idx, int *size);


static void check2test (Instruction *p, int n) {
  assert(ischeck(p) && n != 0);
  p->i.offset = n;
}


/*
** invert array slice p[0]-p[e] (both inclusive)
*/
static void invert (Instruction *p, int e) {
  int i;
  for (i = 0; i < e; i++, e--) {
    Instruction temp = p[i];
    p[i] = p[e];
    p[e] = temp;
  }
}


/*
** rotate array slice p[0]-p[e] (both inclusive) 'n' steps
** to the 'left'
*/
static void rotate (Instruction *p, int e, int n) {
  invert(p, n - 1);
  invert(p + n, e - n);
  invert(p, e);
}


#define op_step(p)	((p)->i.code == IAny ? (p)->i.aux : 1)


static int skipchecks (Instruction *p, int up, int *pn) {
  int i, n = 0;
  for (i = 0; isfixcheck(p + i); i += sizei(p + i)) {
    int st = op_step(p + i);
    if (n + st > MAXOFF - up) break;
    n += st;
  }
  *pn = n;
  return i;
}


#define ismovablecap(op)	(ismovable(op) && getoff(op) < MAXOFF)

static void optimizecaptures (Instruction *p) {
  int i;
  int limit = 0;
  for (i = 0; p[i].i.code != IEnd; i += sizei(p + i)) {
    if (isjmp(p + i) && dest(p, i) >= limit)
      limit = dest(p, i) + 1;  /* do not optimize jump targets */
    else if (i >= limit && ismovablecap(p + i) && isfixcheck(p + i + 1)) {
      int end, n, j;  /* found a border capture|check */
      int maxoff = getoff(p + i);
      int start = i;
      /* find first capture in the group */
      while (start > limit && ismovablecap(p + start - 1)) {
        start--;
        if (getoff(p + start) > maxoff) maxoff = getoff(p + start);
      }
      end = skipchecks(p + i + 1, maxoff, &n) + i;  /* find last check */
      if (n == 0) continue;  /* first check is too big to move across */
      assert(n <= MAXOFF && start <= i && i < end);
      for (j = start; j <= i; j++)
        p[j].i.aux += (n << 4);  /* correct offset of captures to be moved */
      rotate(p + start, end - start, i - start + 1);  /* move them up */
      i = end;
      assert(isfixcheck(p + start) && iscapture(p + i));
    }
  }
}


static int target (Instruction *p, int i) {
  while (p[i].i.code == IJmp)  i += p[i].i.offset;
  return i;
}


static void optimizejumps (Instruction *p) {
  int i;
  for (i = 0; p[i].i.code != IEnd; i += sizei(p + i)) {
    if (isjmp(p + i))
      p[i].i.offset = target(p, dest(p, i)) - i;
  }
}


static void optimizechoice (Instruction *p) {
  assert(p->i.code == IChoice);
  if (isfixcheck(p + 1)) {
    int lc = sizei(p + 1);
    rotate(p, lc, 1);
    assert(isfixcheck(p) && (p + lc)->i.code == IChoice);
    (p + lc)->i.aux = op_step(p);
    check2test(p, (p + lc)->i.offset);
    (p + lc)->i.offset -= lc;
  }
}


/*
** A 'headfail' pattern is a pattern that can only fails in its first
** instruction, which must be a check.
*/
static int isheadfail (Instruction *p) {
  if (!ischeck(p)) return 0;
  /* check that other operations cannot fail */
  for (p += sizei(p); p->i.code != IEnd; p += sizei(p))
    if (!isnofail(p)) return 0;
  return 1;
}


#define checkpattern(L, idx) ((Instruction *)luaL_checkudata(L, idx, PATTERN_T))


/*
** Return the number of elements in the ktable of a pattern.
** in Lua 5.2, default "environment" for patterns is nil, not
** a table. Treat it as an empty table.
*/
static int ktablelen (lua_State *L, int idx) {
  if (!lua_istable(L, idx)) return 0;
  else return lua_objlen(L, idx);
}


/*
** join the elements of the ktable from pattern 'p1' into the ktable of
** the pattern at the top of the stack ('p').  If 'p1' has no elements,
** 'p' keeps its original ktable.  If 'p' has no elements, it shares
** 'p1' ktable.  Otherwise, this function creates a new ktable for 'p'.
** Return the offset of original 'p' elements in the new ktable.
*/ 
static int jointable (lua_State *L, int p1) {
  int n, n1, i;
  lua_getfenv(L, p1);
  n1 = ktablelen(L, -1);  /* number of elements in p1's env */
  lua_getfenv(L, -2);
  if (n1 == 0 || lua_equal(L, -2, -1)) {
    lua_pop(L, 2);
    return 0;  /* no need to change anything */
  }
  n = ktablelen(L, -1);  /* number of elements in p's env */
  if (n == 0) {
    lua_pop(L, 1);  /* removes p env */
    lua_setfenv(L, -2);  /* p now shares p1's env */
    return 0;  /* no need to correct anything */
  }
  lua_createtable(L, n + n1, 0);
  /* stack: p; p1 env; p env; new p env */
  for (i = 1; i <= n; i++) {
    lua_rawgeti(L, -2, i);
    lua_rawseti(L, -2, i);
  }
  for (i = 1; i <= n1; i++) {
    lua_rawgeti(L, -3, i);
    lua_rawseti(L, -2, n + i);
  }
  lua_setfenv(L, -4);  /* new table becomes p env */
  lua_pop(L, 2);  /* remove p1 env and old p env */
  return n;
}


#define copypatt(p1,p2,sz)	memcpy(p1, p2, (sz) * sizeof(Instruction));

#define pattsize(L,idx)		(lua_objlen(L, idx)/sizeof(Instruction) - 1)


static int addpatt (lua_State *L, Instruction *p, int p1idx) {
  Instruction *p1 = (Instruction *)lua_touserdata(L, p1idx);
  int sz = pattsize(L, p1idx);
  int corr = jointable(L, p1idx);
  copypatt(p, p1, sz + 1);
  if (corr != 0) {
    Instruction *px;
    for (px = p; px < p + sz; px += sizei(px)) {
      if (isfenvoff(px) && px->i.offset != 0)
        px->i.offset += corr;
    }
  }
  return sz;
}


static void setinstaux (Instruction *i, Opcode op, int offset, int aux) {
  assert(aux <= MAXAUX);
  i->i.code = op;
  i->i.offset = offset;
  i->i.aux = aux;
}

#define setinst(i,op,off)	setinstaux(i,op,off,0)

#define setinstcap(i,op,idx,k,n)  setinstaux(i,op,idx,((k) | ((n) << 4)))


/*
** create a new ktable for pattern at the stack top, mapping
** '1' to the value at stack position 'vidx'.
*/
static int value2fenv (lua_State *L, int vidx) {
  lua_createtable(L, 1, 0);
  lua_pushvalue(L, vidx);
  lua_rawseti(L, -2, 1);
  lua_setfenv(L, -2);
  return 1;
}


static Instruction *newpatt (lua_State *L, size_t n) {
  Instruction *p;
  if (n >= MAXPATTSIZE - 1)
    luaL_error(L, "pattern too big");
  p = (Instruction *)lua_newuserdata(L, (n + 1) * sizeof(Instruction));
  luaL_getmetatable(L, PATTERN_T);
  lua_setmetatable(L, -2);
  setinst(p + n, IEnd, 0);
  return p;
}


static void fillcharset (Instruction *p, Charset cs) {
  switch (p[0].i.code) {
    case ISet: {
      loopset(i, cs[i] = p[1].buff[i]);
      break;
    }
    case IChar: {
      loopset(i, cs[i] = 0);
      setchar(cs, p[0].i.aux);
      break;
    }
    default: {  /* any char may start unhandled instructions */
      loopset(i, cs[i] = 0xff);
      break;
    }
  }
}


/*
** Function 'tocharset' gets information about which chars may be a
** valid start for a pattern.
*/

static enum charsetanswer tocharset (Instruction *p, CharsetTag *c) {
  if (isfixcheck(p)) {
    fillcharset(p, c->cs);
    if ((p + sizei(p))->i.code == IEnd && op_step(p) == 1)
      c->tag = ISCHARSET;
    else
      c->tag = VALIDSTARTS;
  }
  else
    c->tag = NOINFO;
  return c->tag;
}


static int exclusiveset (Charset c1, Charset c2) {
  /* non-empty intersection? */
  loopset(i, {if ((c1[i] & c2[i]) != 0) return 0;});
  return 1;  /* no intersection */
}


static int exclusive (CharsetTag *c1, CharsetTag *c2) {
  if (c1->tag == NOINFO || c2->tag == NOINFO)
    return 0;  /* one of them is not filled */
  else return exclusiveset(c1->cs, c2->cs);
}


static Instruction *newcharset (lua_State *L) {
  Instruction *p = newpatt(L, CHARSETINSTSIZE);
  p[0].i.code = ISet;
  p[0].i.offset = 0;
  loopset(i, p[1].buff[i] = 0);
  return p;
}


static int set_l (lua_State *L) {
  size_t l;
  const char *s = luaL_checklstring(L, 1, &l);
  if (l == 1)
    getpatt(L, 1, NULL);  /* a unit set is equivalent to a literal */
  else {
    Instruction *p = newcharset(L);
    while (l--) {
      setchar(p[1].buff, (byte)(*s));
      s++;
    }
  }
  return 1;
}


static int range_l (lua_State *L) {
  int arg;
  int top = lua_gettop(L);
  Instruction *p = newcharset(L);
  for (arg = 1; arg <= top; arg++) {
    int c;
    size_t l;
    const char *r = luaL_checklstring(L, arg, &l);
    luaL_argcheck(L, l == 2, arg, "range must have two characters");
    for (c = (byte)r[0]; c <= (byte)r[1]; c++)
      setchar(p[1].buff, c);
  }
  return 1;
}


static int nter_l (lua_State *L) {
  Instruction *p;
  luaL_argcheck(L, !lua_isnoneornil(L, 1), 1, "non-nil value expected");
  p = newpatt(L, 1);
  setinst(p, IOpenCall, value2fenv(L, 1));
  return 1;
}



static int testpattern (lua_State *L, int idx) {
  if (lua_touserdata(L, idx)) {  /* value is a userdata? */
    if (lua_getmetatable(L, idx)) {  /* does it have a metatable? */
      luaL_getmetatable(L, PATTERN_T);
      if (lua_rawequal(L, -1, -2)) {  /* does it have the correct mt? */
        lua_pop(L, 2);  /* remove both metatables */
        return 1;
      }
    }
  }
  return 0;
}


static Instruction *fix_l (lua_State *L, int t) {
  Instruction *p;
  int i;
  int totalsize = 2;  /* include initial call and jump */
  int n = 0;  /* number of rules */
  int base = lua_gettop(L);
  lua_newtable(L);  /* to store relative positions of each rule */
  lua_pushinteger(L, 1);  /* default initial rule */
  /* collect patterns and compute sizes */
  lua_pushnil(L);
  while (lua_next(L, t) != 0) {
    int l;
    if (lua_tonumber(L, -2) == 1 && lua_isstring(L, -1)) {
      lua_replace(L, base + 2);  /* use this value as initial rule */
      continue;
    }
    if (!testpattern(L, -1))
      luaL_error(L, "%s is not a pattern", val2str(L, -2));
    l = pattsize(L, -1) + 1;  /* space for pattern + ret */
    if (totalsize >= MAXPATTSIZE - l)
      luaL_error(L, "grammar too large");
    luaL_checkstack(L, LUA_MINSTACK, "grammar has too many rules");
    lua_insert(L, -2);  /* put key on top */
    lua_pushvalue(L, -1);  /* duplicate key (for lua_next) */
    lua_pushvalue(L, -1);  /* duplicate key (to index positions table)) */
    lua_pushinteger(L, totalsize);  /* position for this rule */
    lua_settable(L, base + 1);  /* store key=>position in positions table */
    totalsize += l;
    n++;
  }
  luaL_argcheck(L, n > 0, t, "empty grammar");
  p = newpatt(L, totalsize);  /* create new pattern */
  p++;  /* save space for call */
  setinst(p++, IJmp, totalsize - 1);  /* after call, jumps to the end */
  for (i = 1; i <= n; i++) {  /* copy all rules into new pattern */
    p += addpatt(L, p, base + 1 + i*2);
    setinst(p++, IRet, 0);
  }
  p -= totalsize;  /* back to first position */
  totalsize = 2;  /* go through each rule's position */
  for (i = 1; i <= n; i++) {  /* check all rules */
    int l = pattsize(L, base + 1 + i*2) + 1;
    checkrule(L, p, totalsize, totalsize + l, base + 1, base + 2 + i*2);
    totalsize += l;
  }
  lua_pushvalue(L, base + 2);  /* get initial rule */
  lua_gettable(L, base + 1);  /* get its position in postions table */
  i = lua_tonumber(L, -1);  /* convert to number */
  lua_pop(L, 1);
  if (i == 0)  /* is it defined? */
    luaL_error(L, "initial rule not defined in given grammar");
  setinst(p, ICall, i);  /* first instruction calls initial rule */
  /* correct calls */
  for (i = 0; i < totalsize; i += sizei(p + i)) {
    if (p[i].i.code == IOpenCall) {
      int pos = getposition(L, base + 1, p[i].i.offset);
      p[i].i.code = (p[target(p, i + 1)].i.code == IRet) ? IJmp : ICall;
      p[i].i.offset = pos - i;
    }
  }
  optimizejumps(p);
  lua_replace(L, t);  /* put new pattern in old's position */
  lua_settop(L, base);  /* remove rules and positions table */
  return p;
}


static Instruction *any (lua_State *L, int n, int extra, int *offsetp) {
  int offset = offsetp ? *offsetp : 0;
  Instruction *p = newpatt(L, (n - 1)/UCHAR_MAX + extra + 1);
  Instruction *p1 = p + offset;
  for (; n > UCHAR_MAX; n -= UCHAR_MAX)
    setinstaux(p1++, IAny, 0, UCHAR_MAX);
  setinstaux(p1++, IAny, 0, n);
  if (offsetp) *offsetp = p1 - p;
  return p;
}


static Instruction *getpatt (lua_State *L, int idx, int *size) {
  Instruction *p;
  switch (lua_type(L, idx)) {
    case LUA_TSTRING: {
      size_t i, len;
      const char *s = lua_tolstring(L, idx, &len);
      p = newpatt(L, len);
      for (i = 0; i < len; i++)
        setinstaux(p + i, IChar, 0, (byte)s[i]);
      lua_replace(L, idx);
      break;
    }
    case LUA_TNUMBER: {
      int n = lua_tointeger(L, idx);
      if (n == 0)  /* empty pattern? */
        p = newpatt(L, 0);
      else if (n > 0)
        p = any(L, n, 0, NULL);
      else if (-n <= UCHAR_MAX) {
        p = newpatt(L, 2);
        setinstaux(p, IAny, 2, -n);
        setinst(p + 1, IFail, 0);
      }
      else {
        int offset = 2;  /* space for ITestAny & IChoice */
        p = any(L, -n - UCHAR_MAX, 3, &offset);
        setinstaux(p, IAny, offset + 1, UCHAR_MAX);
        setinstaux(p + 1, IChoice, offset, UCHAR_MAX);
        setinst(p + offset, IFailTwice, 0);
      }
      lua_replace(L, idx);
      break;
    }
    case LUA_TBOOLEAN: {
      if (lua_toboolean(L, idx))  /* true? */
        p = newpatt(L, 0);  /* empty pattern (always succeeds) */
      else {
        p = newpatt(L, 1);
        setinst(p, IFail, 0);
      }
      lua_replace(L, idx);
      break;
    }
    case LUA_TTABLE: {
      p = fix_l(L, idx);
      break;
    }
    case LUA_TFUNCTION: {
      p = newpatt(L, 2);
      setinstcap(p, IOpenCapture, value2fenv(L, idx), Cruntime, 0);
      setinstcap(p + 1, ICloseRunTime, 0, Cclose, 0);
      lua_replace(L, idx);
      break;
    }
    default: {
      p = checkpattern(L, idx);
      break;
    }
  }
  if (size) *size = pattsize(L, idx);
  return p;
}


static int getpattl (lua_State *L, int idx) {
  int size;
  getpatt(L, idx, &size);
  return size;
}


static int pattern_l (lua_State *L) {
  lua_settop(L, 1);
  getpatt(L, 1, NULL);
  return 1;
}


#define isany(p)	((p)->i.code == IAny && ((p) + 1)->i.code == IEnd)
#define isfail(p)	((p)->i.code == IFail)
#define issucc(p)	((p)->i.code == IEnd)

static int concat_l (lua_State *L) {
  /* p1; p2; */
  int l1, l2;
  Instruction *p1 = getpatt(L, 1, &l1);
  Instruction *p2 = getpatt(L, 2, &l2);
  if (isfail(p1) || issucc(p2))
    lua_pushvalue(L, 1);  /* fail * x == fail; x * true == x */
  else if (isfail(p2) || issucc(p1))
    lua_pushvalue(L, 2);  /* x * fail == fail; true * x == x */
  else if (isany(p1) && isany(p2))
    any(L, p1->i.aux + p2->i.aux, 0, NULL);
  else {
    Instruction *op = newpatt(L, l1 + l2);
    Instruction *p = op + addpatt(L, op, 1);
    addpatt(L, p, 2);
    optimizecaptures(op);
  }
  return 1;
}


static int diff_l (lua_State *L) {
  int l1, l2;
  Instruction *p1 = getpatt(L, 1, &l1);
  Instruction *p2 = getpatt(L, 2, &l2);
  CharsetTag st1, st2;
  if (tocharset(p1, &st1) == ISCHARSET && tocharset(p2, &st2) == ISCHARSET) {
    Instruction *p = newcharset(L);
    loopset(i, p[1].buff[i] = st1.cs[i] & ~st2.cs[i]);
  }
  else if (isheadfail(p2)) {
    Instruction *p = newpatt(L, l2 + 1 + l1);
    p += addpatt(L, p, 2);
    check2test(p - l2, l2 + 1);
    setinst(p++, IFail, 0);
    addpatt(L, p, 1);
  }
  else {  /* !e2 . e1 */
    /* !e -> choice L1; e; failtwice; L1: ... */
    Instruction *p = newpatt(L, 1 + l2 + 1 + l1);
    Instruction *pi = p;
    setinst(p++, IChoice, 1 + l2 + 1);
    p += addpatt(L, p, 2);
    setinst(p++, IFailTwice, 0);
    addpatt(L, p, 1);
    optimizechoice(pi);
  }
  return 1;
}


static int unm_l (lua_State *L) {
  Instruction *p = getpatt(L, 1, NULL);
  if (isfail(p)) {  /* -false? */
    newpatt(L, 0);  /* true */
    return 1;
  }
  else if (issucc(p)) {  /* -true? */
    Instruction *p1 = newpatt(L, 1);  /* false */
    setinst(p1, IFail, 0);
    return 1;
  }
  else {  /* -A == '' - A */
    lua_pushliteral(L, "");
    lua_insert(L, 1);
    return diff_l(L);
  }
}


static int pattand_l (lua_State *L) {
  int l1;
  CharsetTag st1;
  Instruction *p1 = getpatt(L, 1, &l1);
  if (isfail(p1) || issucc(p1))
    lua_pushvalue(L, 1);  /* &fail == fail; &true == true */
  else if (tocharset(p1, &st1) == ISCHARSET) {
    Instruction *p = newpatt(L, l1 + 1);
    copypatt(p, p1, l1); p += l1;
    setinstaux(p, IBack, 0, 1);
  }
  else {  /* Choice L1; p1; BackCommit L2; L1: Fail; L2: */
    Instruction *p = newpatt(L, 1 + l1 + 2);
    setinst(p++, IChoice, 1 + l1 + 1);
    p += addpatt(L, p, 1);
    setinst(p++, IBackCommit, 2);
    setinst(p, IFail, 0);
  }
  return 1;
}


static int nocalls (const Instruction *p) {
  for (; (Opcode)p->i.code != IEnd; p += sizei(p))
    if ((Opcode)p->i.code == IOpenCall) return 0;
  return 1;
}


static int pattbehind (lua_State *L) {
  int l1;
  CharsetTag st1;
  Instruction *p1 = getpatt(L, 1, &l1);
  int n = luaL_optint(L, 2, 1);
  luaL_argcheck(L, n <= MAXAUX, 2, "lookbehind delta too large");
  if (!nocalls(p1))
    luaL_error(L, "lookbehind pattern cannot contain non terminals");
  if (isfail(p1) || issucc(p1))
    lua_pushvalue(L, 1);  /* <fail == fail; <true == true */
  else if (n == 1 && tocharset(p1, &st1) == ISCHARSET) {
    Instruction *p = newpatt(L, 1 + l1);
    setinstaux(p, IBack, 0, 1); p++;
    copypatt(p, p1, l1);
  }
  else {  /* Choice L1; Back; p1; BackCommit L2; L1: fail; L2: */
    Instruction *p = newpatt(L, 2 + l1 + 2);
    setinst(p++, IChoice, 2 + l1 + 1);
    setinstaux(p++, IBack, 0, n);
    p += addpatt(L, p, 1);
    setinst(p++, IBackCommit, 2);
    setinst(p, IFail, 0);
  }
  return 1;
}



static int firstpart (Instruction *p, int l) {
  if (istest(p)) {
    int e = p[0].i.offset - 1;
    if ((p[e].i.code == IJmp || p[e].i.code == ICommit) &&
        e + p[e].i.offset == l)
      return e + 1;
  }
  else if (p[0].i.code == IChoice) {
    int e = p[0].i.offset - 1;
    if (p[e].i.code == ICommit && e + p[e].i.offset == l)
      return e + 1;
  }
  return 0;
}


static Instruction *auxnew (lua_State *L, Instruction **op, int *size,
                                         int extra) {
  *op = newpatt(L, *size + extra);
  jointable(L, 1);
  *size += extra;
  return *op + *size - extra;
}


static int nofail (Instruction *p, int l) {
  int i;
  for (i = 0; i < l; i += sizei(p + i)) {
    if (!isnofail(p + i)) return 0;
  }
  return 1;
}


static int interfere (Instruction *p1, int l1, CharsetTag *st2) {
  if (nofail(p1, l1))  /* p1 cannot fail? */
    return 0;  /* nothing can intefere with it */
  if (st2->tag == NOINFO) return 1;
  assert(p1->i.offset != 0);
  switch (p1->i.code) {
    case IChar: return testchar(st2->cs, p1->i.aux);
    case ISet: return !exclusiveset(st2->cs, (p1 + 1)->buff);
    default: assert(p1->i.code == IAny); return 1;
  }
}


static Instruction *basicUnion (lua_State *L, Instruction *p1, int l1,
                                int l2, int *size, CharsetTag *st2) {
  Instruction *op;
  CharsetTag st1;
  tocharset(p1, &st1);
  if (st1.tag == ISCHARSET && st2->tag == ISCHARSET) {
    Instruction *p = auxnew(L, &op, size, CHARSETINSTSIZE);
    setinst(p, ISet, 0);
    loopset(i, p[1].buff[i] = st1.cs[i] | st2->cs[i]);
  }
  else if (exclusive(&st1, st2) || isheadfail(p1)) {
    Instruction *p = auxnew(L, &op, size, l1 + 1 + l2);
    copypatt(p, p1, l1);
    check2test(p, l1 + 1);
    p += l1;
    setinst(p++, IJmp, l2 + 1);
    addpatt(L, p, 2);
  }
  else {
    /* choice L1; e1; commit L2; L1: e2; L2: ... */
    Instruction *p = auxnew(L, &op, size, 1 + l1 + 1 + l2);
    setinst(p++, IChoice, 1 + l1 + 1);
    copypatt(p, p1, l1); p += l1;
    setinst(p++, ICommit, 1 + l2);
    addpatt(L, p, 2);
    optimizechoice(p - (1 + l1 + 1));
  }
  return op;
}


static Instruction *separateparts (lua_State *L, Instruction *p1, int l1,
                                   int l2, int *size, CharsetTag *st2) {
  int sp = firstpart(p1, l1);
  if (sp == 0)  /* first part is entire p1? */
    return basicUnion(L, p1, l1, l2, size, st2);
  else if ((p1 + sp - 1)->i.code == ICommit || !interfere(p1, sp, st2)) {
    Instruction *p;
    int init = *size;
    int end = init + sp;
    *size = end;
    p = separateparts(L, p1 + sp, l1 - sp, l2, size, st2);
    copypatt(p + init, p1, sp);
    (p + end - 1)->i.offset = *size - (end - 1);
    return p;
  }
  else {  /* must change back to non-optimized choice */
    Instruction *p;
    int init = *size;
    int end = init + sp + 1;  /* needs one extra instruction (choice) */
    int sizefirst = sizei(p1);  /* size of p1's first instruction (the test) */
    *size = end;
    p = separateparts(L, p1 + sp, l1 - sp, l2, size, st2);
    copypatt(p + init, p1, sizefirst);  /* copy the test */
    (p + init)->i.offset++;  /* correct jump (because of new instruction) */
    init += sizefirst;
    setinstaux(p + init, IChoice, sp - sizefirst + 1, 1); init++;
    copypatt(p + init, p1 + sizefirst, sp - sizefirst - 1);
    init += sp - sizefirst - 1;
    setinst(p + init, ICommit, *size - (end - 1));
    return p;
  }
}


static int union_l (lua_State *L) {
  int l1, l2;
  int size = 0;
  Instruction *p1 = getpatt(L, 1, &l1);
  Instruction *p2 = getpatt(L, 2, &l2);
  CharsetTag st2;
  if (isfail(p1))  /* check for simple identities */
    lua_pushvalue(L, 2);  /* fail / a == a */
  else if (isfail(p2) || issucc(p1))
    lua_pushvalue(L, 1);  /* a / fail == a; true / a == true */
  else {
    tocharset(p2, &st2);
    separateparts(L, p1, l1, l2, &size, &st2);
  }
  return 1;
}


static int repeatcharset (lua_State *L, Charset cs, int l1, int n) {
  /* e; ...; e; span; */
  int i;
  Instruction *p = newpatt(L, n*l1 + CHARSETINSTSIZE);
  for (i = 0; i < n; i++) {
    p += addpatt(L, p, 1);
  }
  setinst(p, ISpan, 0);
  loopset(k, p[1].buff[k] = cs[k]);
  return 1;
}


static Instruction *repeatheadfail (lua_State *L, int l1, int n) {
  /* e; ...; e; L2: e'(L1); jump L2; L1: ... */
  int i;
  Instruction *p = newpatt(L, (n + 1)*l1 + 1);
  Instruction *op = p;
  for (i = 0; i < n; i++) {
    p += addpatt(L, p, 1);
  }
  p += addpatt(L, p, 1);
  check2test(p - l1, l1 + 1);
  setinst(p, IJmp, -l1);
  return op;
}


static Instruction *repeats (lua_State *L, Instruction *p1, int l1, int n) {
  /* e; ...; e; choice L1; L2: e; partialcommit L2; L1: ... */
  int i;
  Instruction *op = newpatt(L, (n + 1)*l1 + 2);
  Instruction *p = op;
  if (!verify(L, p1, p1, p1 + l1, 0, 0))
    luaL_error(L, "loop body may accept empty string");
  for (i = 0; i < n; i++) {
    p += addpatt(L, p, 1);
  }
  setinst(p++, IChoice, 1 + l1 + 1);
  p += addpatt(L, p, 1);
  setinst(p, IPartialCommit, -l1);
  return op;
}


static void optionalheadfail (lua_State *L, int l1, int n) {
  Instruction *op = newpatt(L, n * l1);
  Instruction *p = op;
  int i;
  for (i = 0; i < n; i++) {
    p += addpatt(L, p, 1);
    check2test(p - l1, (n - i)*l1);
  }
}


static void optionals (lua_State *L, int l1, int n) {
  /* choice L1; e; partialcommit L2; L2: ... e; L1: commit L3; L3: ... */
  int i;
  Instruction *op = newpatt(L, n*(l1 + 1) + 1);
  Instruction *p = op;
  setinst(p++, IChoice, 1 + n*(l1 + 1));
  for (i = 0; i < n; i++) {
    p += addpatt(L, p, 1);
    setinst(p++, IPartialCommit, 1);
  }
  setinst(p - 1, ICommit, 1);  /* correct last commit */
  optimizechoice(op);
}


static int star_l (lua_State *L) {
  int l1;
  int n = luaL_checkint(L, 2);
  Instruction *p1 = getpatt(L, 1, &l1);
  if (n >= 0) {
    CharsetTag st;
    Instruction *op;
    if (tocharset(p1, &st) == ISCHARSET)
      return repeatcharset(L, st.cs, l1, n);
    if (isheadfail(p1))
      op = repeatheadfail(L, l1, n);
    else
      op = repeats(L, p1, l1, n);
    optimizecaptures(op);
    optimizejumps(op);
  }
  else {
    if (isheadfail(p1))
      optionalheadfail(L, l1, -n);
    else
      optionals(L, l1, -n);
  }
  return 1;
}


static int getlabel (lua_State *L, int labelidx) {
  if (labelidx == 0) return 0;
  else return value2fenv(L, labelidx);
}


static int capture_aux (lua_State *L, int kind, int labelidx) {
  int l1, n;
  Instruction *p1 = getpatt(L, 1, &l1);
  int lc = skipchecks(p1, 0, &n);
  if (lc == l1) {  /* got whole pattern? */
    /* may use a IFullCapture instruction at its end */
    Instruction *p = newpatt(L, l1 + 1);
    int label = getlabel(L, labelidx);
    p += addpatt(L, p, 1);
    setinstcap(p, IFullCapture, label, kind, n);
  }
  else {  /* must use open-close pair */
    Instruction *op = newpatt(L, 1 + l1 + 1);
    Instruction *p = op;
    setinstcap(p++, IOpenCapture, getlabel(L, labelidx), kind, 0);
    p += addpatt(L, p, 1);
    setinstcap(p, ICloseCapture, 0, Cclose, 0);
    optimizecaptures(op);
  }
  return 1;
}


static int capture_l (lua_State *L) { return capture_aux(L, Csimple, 0); }
static int tcapture_l (lua_State *L) { return capture_aux(L, Ctable, 0); }
static int capsubst_l (lua_State *L) { return capture_aux(L, Csubst, 0); }

static int rcapture_l (lua_State *L) {
  switch (lua_type(L, 2)) {
    case LUA_TFUNCTION: return capture_aux(L, Cfunction, 2);
    case LUA_TTABLE: return capture_aux(L, Cquery, 2);
    case LUA_TSTRING: return capture_aux(L, Cstring, 2);
    default: return luaL_argerror(L, 2, "invalid replacement value");
  }
}


static int fold_l (lua_State *L) {
  luaL_checktype(L, 2, LUA_TFUNCTION);
  return capture_aux(L, Cfold, 2);
}


static int group_l (lua_State *L) {
  if (lua_isnoneornil(L, 2))
    return capture_aux(L, Cgroup, 0);
  else {
    luaL_checkstring(L, 2);
    return capture_aux(L, Cgroup, 2);
  }
}


static int position_l (lua_State *L) {
  Instruction *p = newpatt(L, 1);
  setinstcap(p, IEmptyCapture, 0, Cposition, 0);
  return 1;
}


static int backref_l (lua_State *L) {
  Instruction *p = newpatt(L, 1);
  int n = getlabel(L, 1);
  setinstcap(p, IEmptyCaptureIdx, n, Cbackref, 0);
  return 1;
}


static int argcap_l (lua_State *L) {
  int n = luaL_checkint(L, 1);
  Instruction *p = newpatt(L, 1);
  luaL_argcheck(L, 0 < n && n <= SHRT_MAX, 1, "invalid argument index");
  setinstcap(p, IEmptyCapture, n, Carg, 0);
  return 1;
}


static int matchtime_l (lua_State *L) {
  int l1 = getpattl(L, 1);
  Instruction *op = newpatt(L, 1 + l1 + 1);
  Instruction *p = op;
  luaL_checktype(L, 2, LUA_TFUNCTION);
  setinstcap(p++, IOpenCapture, value2fenv(L, 2), Cruntime, 0);
  p += addpatt(L, p, 1);
  setinstcap(p, ICloseRunTime, 0, Cclose, 0);
  optimizecaptures(op);
  return 1;
}


static int capconst_l (lua_State *L) {
  int i, j;
  int n = lua_gettop(L);
  Instruction *p = newpatt(L, n > 1 ? n + 2 : n);
  lua_createtable(L, n, 0);  /* new environment for the new pattern */
  if (n > 1) setinstcap(p++, IOpenCapture, 0, Cgroup, 0);
  for (i = j = 1; i <= n; i++) {
    if (lua_isnil(L, i))
      setinstcap(p++, IEmptyCaptureIdx, 0, Cconst, 0);
    else {
      setinstcap(p++, IEmptyCaptureIdx, j, Cconst, 0);
      lua_pushvalue(L, i);
      lua_rawseti(L, -2, j++);
    }
  }
  if (n > 1) setinstcap(p++, ICloseCapture, 0, Cclose, 0);
  lua_setfenv(L, -2);   /* set environment */
  return 1;
}


/* }====================================================== */


/*
** {======================================================
** User-Defined Patterns
** =======================================================
*/

static void l_newpf (lua_State *L, PattFunc f, const void *ud, size_t l) {
  int n = instsize(l) + 1;
  Instruction *p = newpatt(L, n);
  if (n > MAXAUX) luaL_error(L, "pattern data too long");
  p[0].i.code = IFunc;
  p[0].i.aux = n - 2;
  p[0].i.offset = 0;
  p[1].f = f;
  memcpy(p[2].buff, ud, l);
}

/* }====================================================== */


/*
** {======================================================
** Captures
** =======================================================
*/


typedef struct CapState {
  Capture *cap;  /* current capture */
  Capture *ocap;  /* (original) capture list */
  lua_State *L;
  int ptop;  /* index of last argument to 'match' */
  const char *s;  /* original string */
  int valuecached;  /* value stored in cache slot */
} CapState;


#define captype(cap)	((cap)->kind)

#define isclosecap(cap)	(captype(cap) == Cclose)

#define closeaddr(c)	((c)->s + (c)->siz - 1)

#define isfullcap(cap)	((cap)->siz != 0)

#define getfromenv(cs,v)	lua_rawgeti((cs)->L, penvidx((cs)->ptop), v)
#define pushluaval(cs)		getfromenv(cs, (cs)->cap->idx)

#define pushsubject(cs, c) lua_pushlstring((cs)->L, (c)->s, (c)->siz - 1)


#define updatecache(cs,v) { if ((v) != (cs)->valuecached) updatecache_(cs,v); }


static void updatecache_ (CapState *cs, int v) {
  getfromenv(cs, v);
  lua_replace(cs->L, subscache(cs));
  cs->valuecached = v;
}


static int pushcapture (CapState *cs);


static Capture *findopen (Capture *cap) {
  int n = 0;
  for (;;) {
    cap--;
    if (isclosecap(cap)) n++;
    else if (!isfullcap(cap))
      if (n-- == 0) return cap;
  }
}


static Capture *nextcap (Capture *cap) {
  if (isfullcap(cap)) return cap + 1;
  else {
    int n = 0;
    for (;;) {
      cap++;
      if (isclosecap(cap)) {
        if (n-- == 0) return cap + 1;
      }
      else if (!isfullcap(cap)) n++;
    }
  }
}


static int pushallvalues (CapState *cs, int addextra) {
  Capture *co = cs->cap;
  int n = 0;
  if (isfullcap(cs->cap++)) {
    pushsubject(cs, co);  /* push whole match */
    return 1;
  }
  while (!isclosecap(cs->cap))
    n += pushcapture(cs);
  if (addextra || n == 0) {  /* need extra? */
    lua_pushlstring(cs->L, co->s, cs->cap->s - co->s);  /* push whole match */
    n++;
  }
  cs->cap++;  /* skip close entry */
  return n;
}


static Capture *findback (CapState *cs, Capture *cap) {
  lua_State *L = cs->L;
  for (;;) {
    if (cap == cs->ocap) {  /* not found */
      const char *s = lua_tostring(L, -1);
      if (s == NULL) s = lua_pushfstring(L, "(a %s)", luaL_typename(L, -1));
      luaL_error(L, "back reference '%s' not found", s);
    }
    cap--;
    if (isclosecap(cap))
      cap = findopen(cap);
    else if (!isfullcap(cap))
      continue; /* opening an enclosing capture: skip and get previous */
    if (captype(cap) == Cgroup) {
      getfromenv(cs, cap->idx);  /* get group name */
      if (lua_equal(L, -2, -1)) {  /* right group? */
        lua_pop(L, 2);  /* remove reference name and group name */
        return cap;
      }
      else lua_pop(L, 1);  /* remove group name */
    }
  }
}


static int backrefcap (CapState *cs) {
  int n;
  Capture *curr = cs->cap;
  pushluaval(cs);  /* reference name */
  cs->cap = findback(cs, curr);
  n = pushallvalues(cs, 0);
  cs->cap = curr + 1;
  return n;
}


static int tablecap (CapState *cs) {
  lua_State *L = cs->L;
  int n = 0;
  lua_newtable(L);
  if (isfullcap(cs->cap++))
    return 1;  /* table is empty */
  while (!isclosecap(cs->cap)) {
    if (captype(cs->cap) == Cgroup && cs->cap->idx != 0) {  /* named group? */
      int k;
      pushluaval(cs);  /* push group name */
      k = pushallvalues(cs, 0);
      if (k == 0) {  /* no value? */
        lua_pop(L, 1);  /* remove group name */
        continue;  /* and go on */
      }
      else if (k > 1)
        lua_pop(L, k - 1);  /* keep just one value */
      lua_settable(L, -3);
    }
    else {
      int i;
      int k = pushcapture(cs);
      for (i = k; i > 0; i--)
        lua_rawseti(L, -(i + 1), n + i);
      n += k;
    }
  }
  cs->cap++;  /* skip close entry */
  return 1;
}


static int querycap (CapState *cs) {
  int idx = cs->cap->idx;
  int n = pushallvalues(cs, 0);
  if (n > 1)  /* extra captures? */
    lua_pop(cs->L, n - 1);  /* throw them away */
  updatecache(cs, idx);
  lua_gettable(cs->L, subscache(cs));
  if (!lua_isnil(cs->L, -1))
    return 1;
  else {
    lua_pop(cs->L, 1);  /* remove value */
    return 0;
  }
}


static int foldcap (CapState *cs) {
  int n;
  lua_State *L = cs->L;
  int idx = cs->cap->idx;
  if (isfullcap(cs->cap++) || isclosecap(cs->cap) || (n = pushcapture(cs)) == 0)
    return luaL_error(L, "no initial value for fold capture");
  if (n > 1)
    lua_pop(L, n - 1);  /* leave only one result */
  while (!isclosecap(cs->cap)) {
    updatecache(cs, idx);
    lua_pushvalue(L, subscache(cs));  /* get folding function */
    lua_insert(L, -2);  /* put it before accumulator */
    n = pushcapture(cs);  /* get other captures */
    lua_call(L, n + 1, 1);  /* call folding function */
  }
  cs->cap++;  /* skip close entry */
  return 1;
}


static int functioncap (CapState *cs) {
  int n;
  int top = lua_gettop(cs->L);
  pushluaval(cs);
  n = pushallvalues(cs, 0);
  lua_call(cs->L, n, LUA_MULTRET);
  return lua_gettop(cs->L) - top;
}


static int runtimecap (lua_State *L, Capture *close, Capture *ocap,
                       const char *o, const char *s, int ptop) {
  CapState cs;
  int n;
  Capture *open = findopen(close);
  assert(captype(open) == Cruntime);
  close->kind = Cclose;
  close->s = s;
  cs.ocap = ocap; cs.cap = open; cs.L = L;
  cs.s = o; cs.valuecached = 0; cs.ptop = ptop;
  luaL_checkstack(L, 4, "too many runtime captures");
  pushluaval(&cs);
  lua_pushvalue(L, SUBJIDX);  /* push original subject */
  lua_pushinteger(L, s - o + 1);  /* current position */
  n = pushallvalues(&cs, 0);
  lua_call(L, n + 2, LUA_MULTRET);
  return close - open;
}



typedef struct StrAux {
  int isstring;
  union {
    Capture *cp;
    struct {
      const char *s;
      const char *e;
    } s;
  } u;
} StrAux;

#define MAXSTRCAPS	10

static int getstrcaps (CapState *cs, StrAux *cps, int n) {
  int k = n++;
  cps[k].isstring = 1;
  cps[k].u.s.s = cs->cap->s;
  if (!isfullcap(cs->cap++)) {
    while (!isclosecap(cs->cap)) {
      if (n >= MAXSTRCAPS)  /* too many captures? */
        cs->cap = nextcap(cs->cap);  /* skip it */
      else if (captype(cs->cap) == Csimple)
        n = getstrcaps(cs, cps, n);
      else {
        cps[n].isstring = 0;
        cps[n].u.cp = cs->cap;
        cs->cap = nextcap(cs->cap);
        n++;
      }
    }
    cs->cap++;  /* skip close */
  }
  cps[k].u.s.e = closeaddr(cs->cap - 1);
  return n;
}


/*
** add next capture (which should be a string) to buffer
*/
static int addonestring (luaL_Buffer *b, CapState *cs, const char *what);


static void stringcap (luaL_Buffer *b, CapState *cs) {
  StrAux cps[MAXSTRCAPS];
  int n;
  size_t len, i;
  const char *c;
  updatecache(cs, cs->cap->idx);
  c = lua_tolstring(cs->L, subscache(cs), &len);
  n = getstrcaps(cs, cps, 0) - 1;
  for (i = 0; i < len; i++) {
    if (c[i] != '%' || c[++i] < '0' || c[i] > '9')
      luaL_addchar(b, c[i]);
    else {
      int l = c[i] - '0';
      if (l > n)
        luaL_error(cs->L, "invalid capture index (%d)", l);
      else if (cps[l].isstring)
        luaL_addlstring(b, cps[l].u.s.s, cps[l].u.s.e - cps[l].u.s.s);
      else {
        Capture *curr = cs->cap;
        cs->cap = cps[l].u.cp;
        if (addonestring(b, cs, "capture") == 0)
          luaL_error(cs->L, "no values in capture index %d", l);
        cs->cap = curr;
      }
    }
  }
}


static void substcap (luaL_Buffer *b, CapState *cs) {
  const char *curr = cs->cap->s;
  if (isfullcap(cs->cap))  /* no nested captures? */
    luaL_addlstring(b, curr, cs->cap->siz - 1);  /* keep original text */
  else {
    cs->cap++;
    while (!isclosecap(cs->cap)) {
      const char *next = cs->cap->s;
      luaL_addlstring(b, curr, next - curr);  /* add text up to capture */
      if (addonestring(b, cs, "replacement") == 0)  /* no capture value? */
        curr = next;  /* keep original text in final result */
      else
        curr = closeaddr(cs->cap - 1);  /* continue after match */
    }
    luaL_addlstring(b, curr, cs->cap->s - curr);  /* add last piece of text */
  }
  cs->cap++;  /* go to next capture */
}


static int addonestring (luaL_Buffer *b, CapState *cs, const char *what) {
  switch (captype(cs->cap)) {
    case Cstring:
      stringcap(b, cs);  /* add capture directly to buffer */
      return 1;
    case Csubst:
      substcap(b, cs);  /* add capture directly to buffer */
      return 1;
    default: {
      lua_State *L = cs->L;
      int n = pushcapture(cs);
      if (n > 0) {
        if (n > 1) lua_pop(L, n - 1);  /* only one result */
        if (!lua_isstring(L, -1))
          luaL_error(L, "invalid %s value (a %s)", what, luaL_typename(L, -1));
        luaL_addvalue(b);
      }
      return n;
    }
  }
}


static int pushcapture (CapState *cs) {
  luaL_checkstack(cs->L, 4, "too many captures");
  switch (captype(cs->cap)) {
    case Cposition: {
      lua_pushinteger(cs->L, cs->cap->s - cs->s + 1);
      cs->cap++;
      return 1;
    }
    case Cconst: {
      pushluaval(cs);
      cs->cap++;
      return 1;
    }
    case Carg: {
      int arg = (cs->cap++)->idx;
      if (arg + FIXEDARGS > cs->ptop)
        return luaL_error(cs->L, "reference to absent argument #%d", arg);
      lua_pushvalue(cs->L, arg + FIXEDARGS);
      return 1;
    }
    case Csimple: {
      int k = pushallvalues(cs, 1);
      if (k > 1)
        lua_insert(cs->L, -k);  /* whole match is first result */
      return k;
    }
    case Cruntime: {
      int n = 0;
      while (!isclosecap(cs->cap++)) {
        luaL_checkstack(cs->L, 4, "too many captures");
        lua_pushvalue(cs->L, (cs->cap - 1)->idx);
        n++;
      }
      return n;
    }
    case Cstring: {
      luaL_Buffer b;
      luaL_buffinit(cs->L, &b);
      stringcap(&b, cs);
      luaL_pushresult(&b);
      return 1;
    }
    case Csubst: {
      luaL_Buffer b;
      luaL_buffinit(cs->L, &b);
      substcap(&b, cs);
      luaL_pushresult(&b);
      return 1;
    }
    case Cgroup: {
      if (cs->cap->idx == 0)  /* anonymous group? */
        return pushallvalues(cs, 0);  /* add all nested values */
      else {  /* named group: add no values */
        cs->cap = nextcap(cs->cap);  /* skip capture */
        return 0;
      }
    }
    case Cbackref: return backrefcap(cs);
    case Ctable: return tablecap(cs);
    case Cfunction: return functioncap(cs);
    case Cquery: return querycap(cs);
    case Cfold: return foldcap(cs);
    default: assert(0); return 0;
  }
}


static int getcaptures (lua_State *L, const char *s, const char *r, int ptop) {
  Capture *capture = (Capture *)lua_touserdata(L, caplistidx(ptop));
  int n = 0;
  if (!isclosecap(capture)) {  /* is there any capture? */
    CapState cs;
    cs.ocap = cs.cap = capture; cs.L = L;
    cs.s = s; cs.valuecached = 0; cs.ptop = ptop;
    do {  /* collect their values */
      n += pushcapture(&cs);
    } while (!isclosecap(cs.cap));
  }
  if (n == 0) {  /* no capture values? */
    lua_pushinteger(L, r - s + 1);  /* return only end position */
    n = 1;
  }
  return n;
}

/* }====================================================== */


static int version_l (lua_State *L) {
  lua_pushstring(L, VERSION);
  return 1;
}


static int type_l (lua_State *L) {
  if (testpattern(L, 1))
    lua_pushliteral(L, "pattern");
  else
    lua_pushnil(L);
  return 1;
}


static void createcat (lua_State *L, const char *catname, int (catf) (int)) {
  Instruction *p = newcharset(L);
  int i;
  for (i = 0; i < CHAR_MAX; i++)
    if (catf(i)) setchar(p[1].buff, i);
  lua_setfield(L, -2, catname);
}


static int locale_l (lua_State *L) {
  if (lua_isnoneornil(L, 1)) {
    lua_settop(L, 0);
    lua_createtable(L, 0, 12);
  }
  else {
    luaL_checktype(L, 1, LUA_TTABLE);
    lua_settop(L, 1);
  }
  createcat(L, "alnum", isalnum);
  createcat(L, "alpha", isalpha);
  createcat(L, "cntrl", iscntrl);
  createcat(L, "digit", isdigit);
  createcat(L, "graph", isgraph);
  createcat(L, "lower", islower);
  createcat(L, "print", isprint);
  createcat(L, "punct", ispunct);
  createcat(L, "space", isspace);
  createcat(L, "upper", isupper);
  createcat(L, "xdigit", isxdigit);
  return 1;
}


static int setmax (lua_State *L) {
  luaL_optinteger(L, 1, -1);
  lua_settop(L, 1);
  lua_setfield(L, LUA_REGISTRYINDEX, MAXSTACKIDX);
  return 0;
}


static int printpat_l (lua_State *L) {
  Instruction *p = getpatt(L, 1, NULL);
  int n, i;
  lua_getfenv(L, 1);
  n = ktablelen(L, -1);
  printf("[");
  for (i = 1; i <= n; i++) {
    printf("%d = ", i);
    lua_rawgeti(L, -1, i);
    if (lua_isstring(L, -1))
      printf("%s  ", lua_tostring(L, -1));
    else
      printf("%s  ", lua_typename(L, lua_type(L, -1)));
    lua_pop(L, 1);
  }
  printf("]\n");
  printpatt(p);
  return 0;
}


static int matchl (lua_State *L) {
  Capture capture[INITCAPSIZE];
  const char *r;
  size_t l;
  Instruction *p = getpatt(L, 1, NULL);
  const char *s = luaL_checklstring(L, SUBJIDX, &l);
  int ptop = lua_gettop(L);
  lua_Integer ii = luaL_optinteger(L, 3, 1);
  size_t i = (ii > 0) ?
             (((size_t)ii <= l) ? (size_t)ii - 1 : l) :
             (((size_t)-ii <= l) ? l - ((size_t)-ii) : 0);
  lua_pushnil(L);  /* subscache */
  lua_pushlightuserdata(L, capture);  /* caplistidx */
  lua_getfenv(L, 1);  /* penvidx */
  r = match(L, s, s + i, s + l, p, capture, ptop);
  if (r == NULL) {
    lua_pushnil(L);
    return 1;
  }
  return getcaptures(L, s, r, ptop);
}


static struct luaL_Reg pattreg[] = {
  {"match", matchl},
  {"print", printpat_l},
  {"locale", locale_l},
  {"setmaxstack", setmax},
  {"B", pattbehind},
  {"C", capture_l},
  {"Cf", fold_l},
  {"Cc", capconst_l},
  {"Cg", group_l},
  {"Cp", position_l},
  {"Cb", backref_l},
  {"Carg", argcap_l},
  {"Cmt", matchtime_l},
  {"Cs", capsubst_l},
  {"Ct", tcapture_l},
  {"P", pattern_l},
  {"R", range_l},
  {"S", set_l},
  {"V", nter_l},
  {"type", type_l},
  {"version", version_l},
  {NULL, NULL}
};


static struct luaL_Reg metapattreg[] = {
  {"__add", union_l},
  {"__pow", star_l},
  {"__sub", diff_l},
  {"__mul", concat_l},
  {"__div", rcapture_l},
  {"__unm", unm_l},
  {"__len", pattand_l},
  {NULL, NULL}
};


int luaopen_lpeg (lua_State *L);
int luaopen_lpeg (lua_State *L) {
  lua_pushcfunction(L, (lua_CFunction)&l_newpf);  /* new-pattern function */
  lua_setfield(L, LUA_REGISTRYINDEX, KEYNEWPATT);  /* register it */
  luaL_newmetatable(L, PATTERN_T);
  lua_pushnumber(L, MAXBACK);
  lua_setfield(L, LUA_REGISTRYINDEX, MAXSTACKIDX);
  luaL_register(L, NULL, metapattreg);
  luaL_register(L, "lpeg", pattreg);
  lua_pushliteral(L, "__index");
  lua_pushvalue(L, -2);
  lua_settable(L, -4);
  return 1;
}

