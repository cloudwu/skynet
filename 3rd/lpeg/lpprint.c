/*
** $Id: lpprint.c,v 1.9 2015/06/15 16:09:57 roberto Exp $
** Copyright 2007, Lua.org & PUC-Rio  (see 'lpeg.html' for license)
*/

#include <ctype.h>
#include <limits.h>
#include <stdio.h>


#include "lptypes.h"
#include "lpprint.h"
#include "lpcode.h"


#if defined(LPEG_DEBUG)

/*
** {======================================================
** Printing patterns (for debugging)
** =======================================================
*/


void printcharset (const byte *st) {
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
    "query", "string", "num", "substitution", "fold",
    "runtime", "group"};
  printf("%s", modes[kind]);
}


static void printjmp (const Instruction *op, const Instruction *p) {
  printf("-> %d", (int)(p + (p + 1)->offset - op));
}


void printinst (const Instruction *op, const Instruction *p) {
  const char *const names[] = {
    "any", "char", "set",
    "testany", "testchar", "testset",
    "span", "behind",
    "ret", "end",
    "choice", "jmp", "call", "open_call",
    "commit", "partial_commit", "back_commit", "failtwice", "fail", "giveup",
     "fullcapture", "opencapture", "closecapture", "closeruntime"
  };
  printf("%02ld: %s ", (long)(p - op), names[p->i.code]);
  switch ((Opcode)p->i.code) {
    case IChar: {
      printf("'%c'", p->i.aux);
      break;
    }
    case ITestChar: {
      printf("'%c'", p->i.aux); printjmp(op, p);
      break;
    }
    case IFullCapture: {
      printcapkind(getkind(p));
      printf(" (size = %d)  (idx = %d)", getoff(p), p->i.key);
      break;
    }
    case IOpenCapture: {
      printcapkind(getkind(p));
      printf(" (idx = %d)", p->i.key);
      break;
    }
    case ISet: {
      printcharset((p+1)->buff);
      break;
    }
    case ITestSet: {
      printcharset((p+2)->buff); printjmp(op, p);
      break;
    }
    case ISpan: {
      printcharset((p+1)->buff);
      break;
    }
    case IOpenCall: {
      printf("-> %d", (p + 1)->offset);
      break;
    }
    case IBehind: {
      printf("%d", p->i.aux);
      break;
    }
    case IJmp: case ICall: case ICommit: case IChoice:
    case IPartialCommit: case IBackCommit: case ITestAny: {
      printjmp(op, p);
      break;
    }
    default: break;
  }
  printf("\n");
}


void printpatt (Instruction *p, int n) {
  Instruction *op = p;
  while (p < op + n) {
    printinst(op, p);
    p += sizei(p);
  }
}


#if defined(LPEG_DEBUG)
static void printcap (Capture *cap) {
  printcapkind(cap->kind);
  printf(" (idx: %d - size: %d) -> %p\n", cap->idx, cap->siz, cap->s);
}


void printcaplist (Capture *cap, Capture *limit) {
  printf(">======\n");
  for (; cap->s && (limit == NULL || cap < limit); cap++)
    printcap(cap);
  printf("=======\n");
}
#endif

/* }====================================================== */


/*
** {======================================================
** Printing trees (for debugging)
** =======================================================
*/

static const char *tagnames[] = {
  "char", "set", "any",
  "true", "false",
  "rep",
  "seq", "choice",
  "not", "and",
  "call", "opencall", "rule", "grammar",
  "behind",
  "capture", "run-time"
};


void printtree (TTree *tree, int ident) {
  int i;
  for (i = 0; i < ident; i++) printf(" ");
  printf("%s", tagnames[tree->tag]);
  switch (tree->tag) {
    case TChar: {
      int c = tree->u.n;
      if (isprint(c))
        printf(" '%c'\n", c);
      else
        printf(" (%02X)\n", c);
      break;
    }
    case TSet: {
      printcharset(treebuffer(tree));
      printf("\n");
      break;
    }
    case TOpenCall: case TCall: {
      printf(" key: %d\n", tree->key);
      break;
    }
    case TBehind: {
      printf(" %d\n", tree->u.n);
        printtree(sib1(tree), ident + 2);
      break;
    }
    case TCapture: {
      printf(" cap: %d  key: %d  n: %d\n", tree->cap, tree->key, tree->u.n);
      printtree(sib1(tree), ident + 2);
      break;
    }
    case TRule: {
      printf(" n: %d  key: %d\n", tree->cap, tree->key);
      printtree(sib1(tree), ident + 2);
      break;  /* do not print next rule as a sibling */
    }
    case TGrammar: {
      TTree *rule = sib1(tree);
      printf(" %d\n", tree->u.n);  /* number of rules */
      for (i = 0; i < tree->u.n; i++) {
        printtree(rule, ident + 2);
        rule = sib2(rule);
      }
      assert(rule->tag == TTrue);  /* sentinel */
      break;
    }
    default: {
      int sibs = numsiblings[tree->tag];
      printf("\n");
      if (sibs >= 1) {
        printtree(sib1(tree), ident + 2);
        if (sibs >= 2)
          printtree(sib2(tree), ident + 2);
      }
      break;
    }
  }
}


void printktable (lua_State *L, int idx) {
  int n, i;
  lua_getuservalue(L, idx);
  if (lua_isnil(L, -1))  /* no ktable? */
    return;
  n = lua_rawlen(L, -1);
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
  /* leave ktable at the stack */
}

/* }====================================================== */

#endif
