/*
** $Id: lpcode.c $
** Copyright 2007, Lua.org & PUC-Rio  (see 'lpeg.html' for license)
*/

#include <limits.h>


#include "lua.h"
#include "lauxlib.h"

#include "lptypes.h"
#include "lpcode.h"


/* signals a "no-instruction */
#define NOINST		-1



static const Charset fullset_ =
  {{0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}};

static const Charset *fullset = &fullset_;

/*
** {======================================================
** Analysis and some optimizations
** =======================================================
*/

/*
** Check whether a charset is empty (returns IFail), singleton (IChar),
** full (IAny), or none of those (ISet). When singleton, '*c' returns
** which character it is. (When generic set, the set was the input,
** so there is no need to return it.)
*/
static Opcode charsettype (const byte *cs, int *c) {
  int count = 0;  /* number of characters in the set */
  int i;
  int candidate = -1;  /* candidate position for the singleton char */
  for (i = 0; i < CHARSETSIZE; i++) {  /* for each byte */
    int b = cs[i];
    if (b == 0) {  /* is byte empty? */
      if (count > 1)  /* was set neither empty nor singleton? */
        return ISet;  /* neither full nor empty nor singleton */
      /* else set is still empty or singleton */
    }
    else if (b == 0xFF) {  /* is byte full? */
      if (count < (i * BITSPERCHAR))  /* was set not full? */
        return ISet;  /* neither full nor empty nor singleton */
      else count += BITSPERCHAR;  /* set is still full */
    }
    else if ((b & (b - 1)) == 0) {  /* has byte only one bit? */
      if (count > 0)  /* was set not empty? */
        return ISet;  /* neither full nor empty nor singleton */
      else {  /* set has only one char till now; track it */
        count++;
        candidate = i;
      }
    }
    else return ISet;  /* byte is neither empty, full, nor singleton */
  }
  switch (count) {
    case 0: return IFail;  /* empty set */
    case 1: {  /* singleton; find character bit inside byte */
      int b = cs[candidate];
      *c = candidate * BITSPERCHAR;
      if ((b & 0xF0) != 0) { *c += 4; b >>= 4; }
      if ((b & 0x0C) != 0) { *c += 2; b >>= 2; }
      if ((b & 0x02) != 0) { *c += 1; }
      return IChar;
    }
    default: {
       assert(count == CHARSETSIZE * BITSPERCHAR);  /* full set */
       return IAny;
    }
  }
}


/*
** A few basic operations on Charsets
*/
static void cs_complement (Charset *cs) {
  loopset(i, cs->cs[i] = ~cs->cs[i]);
}

static int cs_equal (const byte *cs1, const byte *cs2) {
  loopset(i, if (cs1[i] != cs2[i]) return 0);
  return 1;
}

static int cs_disjoint (const Charset *cs1, const Charset *cs2) {
  loopset(i, if ((cs1->cs[i] & cs2->cs[i]) != 0) return 0;)
  return 1;
}


/*
** If 'tree' is a 'char' pattern (TSet, TChar, TAny), convert it into a
** charset and return 1; else return 0.
*/
int tocharset (TTree *tree, Charset *cs) {
  switch (tree->tag) {
    case TSet: {  /* copy set */
      loopset(i, cs->cs[i] = treebuffer(tree)[i]);
      return 1;
    }
    case TChar: {  /* only one char */
      assert(0 <= tree->u.n && tree->u.n <= UCHAR_MAX);
      loopset(i, cs->cs[i] = 0);  /* erase all chars */
      setchar(cs->cs, tree->u.n);  /* add that one */
      return 1;
    }
    case TAny: {
      loopset(i, cs->cs[i] = 0xFF);  /* add all characters to the set */
      return 1;
    }
    default: return 0;
  }
}


/*
** Visit a TCall node taking care to stop recursion. If node not yet
** visited, return 'f(sib2(tree))', otherwise return 'def' (default
** value)
*/
static int callrecursive (TTree *tree, int f (TTree *t), int def) {
  int key = tree->key;
  assert(tree->tag == TCall);
  assert(sib2(tree)->tag == TRule);
  if (key == 0)  /* node already visited? */
    return def;  /* return default value */
  else {  /* first visit */
    int result;
    tree->key = 0;  /* mark call as already visited */
    result = f(sib2(tree));  /* go to called rule */
    tree->key = key;  /* restore tree */
    return result;
  }
}


/*
** Check whether a pattern tree has captures
*/
int hascaptures (TTree *tree) {
 tailcall:
  switch (tree->tag) {
    case TCapture: case TRunTime:
      return 1;
    case TCall:
      return callrecursive(tree, hascaptures, 0);
    case TRule:  /* do not follow siblings */
      tree = sib1(tree); goto tailcall;
    case TOpenCall: assert(0);
    default: {
      switch (numsiblings[tree->tag]) {
        case 1:  /* return hascaptures(sib1(tree)); */
          tree = sib1(tree); goto tailcall;
        case 2:
          if (hascaptures(sib1(tree)))
            return 1;
          /* else return hascaptures(sib2(tree)); */
          tree = sib2(tree); goto tailcall;
        default: assert(numsiblings[tree->tag] == 0); return 0;
      }
    }
  }
}


/*
** Checks how a pattern behaves regarding the empty string,
** in one of two different ways:
** A pattern is *nullable* if it can match without consuming any character;
** A pattern is *nofail* if it never fails for any string
** (including the empty string).
** The difference is only for predicates and run-time captures;
** for other patterns, the two properties are equivalent.
** (With predicates, &'a' is nullable but not nofail. Of course,
** nofail => nullable.)
** These functions are all convervative in the following way:
**    p is nullable => nullable(p)
**    nofail(p) => p cannot fail
** The function assumes that TOpenCall is not nullable;
** this will be checked again when the grammar is fixed.
** Run-time captures can do whatever they want, so the result
** is conservative.
*/
int checkaux (TTree *tree, int pred) {
 tailcall:
  switch (tree->tag) {
    case TChar: case TSet: case TAny:
    case TFalse: case TOpenCall:
      return 0;  /* not nullable */
    case TRep: case TTrue:
      return 1;  /* no fail */
    case TNot: case TBehind:  /* can match empty, but can fail */
      if (pred == PEnofail) return 0;
      else return 1;  /* PEnullable */
    case TAnd:  /* can match empty; fail iff body does */
      if (pred == PEnullable) return 1;
      /* else return checkaux(sib1(tree), pred); */
      tree = sib1(tree); goto tailcall;
    case TRunTime:  /* can fail; match empty iff body does */
      if (pred == PEnofail) return 0;
      /* else return checkaux(sib1(tree), pred); */
      tree = sib1(tree); goto tailcall;
    case TSeq:
      if (!checkaux(sib1(tree), pred)) return 0;
      /* else return checkaux(sib2(tree), pred); */
      tree = sib2(tree); goto tailcall;
    case TChoice:
      if (checkaux(sib2(tree), pred)) return 1;
      /* else return checkaux(sib1(tree), pred); */
      tree = sib1(tree); goto tailcall;
    case TCapture: case TGrammar: case TRule:
      /* return checkaux(sib1(tree), pred); */
      tree = sib1(tree); goto tailcall;
    case TCall:  /* return checkaux(sib2(tree), pred); */
      tree = sib2(tree); goto tailcall;
    default: assert(0); return 0;
  }
}


/*
** number of characters to match a pattern (or -1 if variable)
*/
int fixedlen (TTree *tree) {
  int len = 0;  /* to accumulate in tail calls */
 tailcall:
  switch (tree->tag) {
    case TChar: case TSet: case TAny:
      return len + 1;
    case TFalse: case TTrue: case TNot: case TAnd: case TBehind:
      return len;
    case TRep: case TRunTime: case TOpenCall:
      return -1;
    case TCapture: case TRule: case TGrammar:
      /* return fixedlen(sib1(tree)); */
      tree = sib1(tree); goto tailcall;
    case TCall: {
      int n1 = callrecursive(tree, fixedlen, -1);
      if (n1 < 0)
        return -1;
      else
        return len + n1;
    }
    case TSeq: {
      int n1 = fixedlen(sib1(tree));
      if (n1 < 0)
        return -1;
      /* else return fixedlen(sib2(tree)) + len; */
      len += n1; tree = sib2(tree); goto tailcall;
    }
    case TChoice: {
      int n1 = fixedlen(sib1(tree));
      int n2 = fixedlen(sib2(tree));
      if (n1 != n2 || n1 < 0)
        return -1;
      else
        return len + n1;
    }
    default: assert(0); return 0;
  };
}


/*
** Computes the 'first set' of a pattern.
** The result is a conservative aproximation:
**   match p ax -> x (for some x) ==> a belongs to first(p)
** or
**   a not in first(p) ==> match p ax -> fail (for all x)
**
** The set 'follow' is the first set of what follows the
** pattern (full set if nothing follows it).
**
** The function returns 0 when this resulting set can be used for
** test instructions that avoid the pattern altogether.
** A non-zero return can happen for two reasons:
** 1) match p '' -> ''            ==> return has bit 1 set
** (tests cannot be used because they would always fail for an empty input);
** 2) there is a match-time capture ==> return has bit 2 set
** (optimizations should not bypass match-time captures).
*/
static int getfirst (TTree *tree, const Charset *follow, Charset *firstset) {
 tailcall:
  switch (tree->tag) {
    case TChar: case TSet: case TAny: {
      tocharset(tree, firstset);
      return 0;
    }
    case TTrue: {
      loopset(i, firstset->cs[i] = follow->cs[i]);
      return 1;  /* accepts the empty string */
    }
    case TFalse: {
      loopset(i, firstset->cs[i] = 0);
      return 0;
    }
    case TChoice: {
      Charset csaux;
      int e1 = getfirst(sib1(tree), follow, firstset);
      int e2 = getfirst(sib2(tree), follow, &csaux);
      loopset(i, firstset->cs[i] |= csaux.cs[i]);
      return e1 | e2;
    }
    case TSeq: {
      if (!nullable(sib1(tree))) {
        /* when p1 is not nullable, p2 has nothing to contribute;
           return getfirst(sib1(tree), fullset, firstset); */
        tree = sib1(tree); follow = fullset; goto tailcall;
      }
      else {  /* FIRST(p1 p2, fl) = FIRST(p1, FIRST(p2, fl)) */
        Charset csaux;
        int e2 = getfirst(sib2(tree), follow, &csaux);
        int e1 = getfirst(sib1(tree), &csaux, firstset);
        if (e1 == 0) return 0;  /* 'e1' ensures that first can be used */
        else if ((e1 | e2) & 2)  /* one of the children has a matchtime? */
          return 2;  /* pattern has a matchtime capture */
        else return e2;  /* else depends on 'e2' */
      }
    }
    case TRep: {
      getfirst(sib1(tree), follow, firstset);
      loopset(i, firstset->cs[i] |= follow->cs[i]);
      return 1;  /* accept the empty string */
    }
    case TCapture: case TGrammar: case TRule: {
      /* return getfirst(sib1(tree), follow, firstset); */
      tree = sib1(tree); goto tailcall;
    }
    case TRunTime: {  /* function invalidates any follow info. */
      int e = getfirst(sib1(tree), fullset, firstset);
      if (e) return 2;  /* function is not "protected"? */
      else return 0;  /* pattern inside capture ensures first can be used */
    }
    case TCall: {
      /* return getfirst(sib2(tree), follow, firstset); */
      tree = sib2(tree); goto tailcall;
    }
    case TAnd: {
      int e = getfirst(sib1(tree), follow, firstset);
      loopset(i, firstset->cs[i] &= follow->cs[i]);
      return e;
    }
    case TNot: {
      if (tocharset(sib1(tree), firstset)) {
        cs_complement(firstset);
        return 1;
      }
      /* else go through */
    }
    case TBehind: {  /* instruction gives no new information */
      /* call 'getfirst' only to check for math-time captures */
      int e = getfirst(sib1(tree), follow, firstset);
      loopset(i, firstset->cs[i] = follow->cs[i]);  /* uses follow */
      return e | 1;  /* always can accept the empty string */
    }
    default: assert(0); return 0;
  }
}


/*
** If 'headfail(tree)' true, then 'tree' can fail only depending on the
** next character of the subject.
*/
static int headfail (TTree *tree) {
 tailcall:
  switch (tree->tag) {
    case TChar: case TSet: case TAny: case TFalse:
      return 1;
    case TTrue: case TRep: case TRunTime: case TNot:
    case TBehind:
      return 0;
    case TCapture: case TGrammar: case TRule: case TAnd:
      tree = sib1(tree); goto tailcall;  /* return headfail(sib1(tree)); */
    case TCall:
      tree = sib2(tree); goto tailcall;  /* return headfail(sib2(tree)); */
    case TSeq:
      if (!nofail(sib2(tree))) return 0;
      /* else return headfail(sib1(tree)); */
      tree = sib1(tree); goto tailcall;
    case TChoice:
      if (!headfail(sib1(tree))) return 0;
      /* else return headfail(sib2(tree)); */
      tree = sib2(tree); goto tailcall;
    default: assert(0); return 0;
  }
}


/*
** Check whether the code generation for the given tree can benefit
** from a follow set (to avoid computing the follow set when it is
** not needed)
*/
static int needfollow (TTree *tree) {
 tailcall:
  switch (tree->tag) {
    case TChar: case TSet: case TAny:
    case TFalse: case TTrue: case TAnd: case TNot:
    case TRunTime: case TGrammar: case TCall: case TBehind:
      return 0;
    case TChoice: case TRep:
      return 1;
    case TCapture:
      tree = sib1(tree); goto tailcall;
    case TSeq:
      tree = sib2(tree); goto tailcall;
    default: assert(0); return 0;
  } 
}

/* }====================================================== */



/*
** {======================================================
** Code generation
** =======================================================
*/


/*
** size of an instruction
*/
int sizei (const Instruction *i) {
  switch((Opcode)i->i.code) {
    case ISet: case ISpan: return CHARSETINSTSIZE;
    case ITestSet: return CHARSETINSTSIZE + 1;
    case ITestChar: case ITestAny: case IChoice: case IJmp: case ICall:
    case IOpenCall: case ICommit: case IPartialCommit: case IBackCommit:
      return 2;
    default: return 1;
  }
}


/*
** state for the compiler
*/
typedef struct CompileState {
  Pattern *p;  /* pattern being compiled */
  int ncode;  /* next position in p->code to be filled */
  lua_State *L;
} CompileState;


/*
** code generation is recursive; 'opt' indicates that the code is being
** generated as the last thing inside an optional pattern (so, if that
** code is optional too, it can reuse the 'IChoice' already in place for
** the outer pattern). 'tt' points to a previous test protecting this
** code (or NOINST). 'fl' is the follow set of the pattern.
*/
static void codegen (CompileState *compst, TTree *tree, int opt, int tt,
                     const Charset *fl);


void realloccode (lua_State *L, Pattern *p, int nsize) {
  void *ud;
  lua_Alloc f = lua_getallocf(L, &ud);
  void *newblock = f(ud, p->code, p->codesize * sizeof(Instruction),
                                  nsize * sizeof(Instruction));
  if (newblock == NULL && nsize > 0)
    luaL_error(L, "not enough memory");
  p->code = (Instruction *)newblock;
  p->codesize = nsize;
}


static int nextinstruction (CompileState *compst) {
  int size = compst->p->codesize;
  if (compst->ncode >= size)
    realloccode(compst->L, compst->p, size * 2);
  return compst->ncode++;
}


#define getinstr(cs,i)		((cs)->p->code[i])


static int addinstruction (CompileState *compst, Opcode op, int aux) {
  int i = nextinstruction(compst);
  getinstr(compst, i).i.code = op;
  getinstr(compst, i).i.aux = aux;
  return i;
}


/*
** Add an instruction followed by space for an offset (to be set later)
*/
static int addoffsetinst (CompileState *compst, Opcode op) {
  int i = addinstruction(compst, op, 0);  /* instruction */
  addinstruction(compst, (Opcode)0, 0);  /* open space for offset */
  assert(op == ITestSet || sizei(&getinstr(compst, i)) == 2);
  return i;
}


/*
** Set the offset of an instruction
*/
static void setoffset (CompileState *compst, int instruction, int offset) {
  getinstr(compst, instruction + 1).offset = offset;
}


/*
** Add a capture instruction:
** 'op' is the capture instruction; 'cap' the capture kind;
** 'key' the key into ktable; 'aux' is the optional capture offset
**
*/
static int addinstcap (CompileState *compst, Opcode op, int cap, int key,
                       int aux) {
  int i = addinstruction(compst, op, joinkindoff(cap, aux));
  getinstr(compst, i).i.key = key;
  return i;
}


#define gethere(compst) 	((compst)->ncode)

#define target(code,i)		((i) + code[i + 1].offset)


/*
** Patch 'instruction' to jump to 'target'
*/
static void jumptothere (CompileState *compst, int instruction, int target) {
  if (instruction >= 0)
    setoffset(compst, instruction, target - instruction);
}


/*
** Patch 'instruction' to jump to current position
*/
static void jumptohere (CompileState *compst, int instruction) {
  jumptothere(compst, instruction, gethere(compst));
}


/*
** Code an IChar instruction, or IAny if there is an equivalent
** test dominating it
*/
static void codechar (CompileState *compst, int c, int tt) {
  if (tt >= 0 && getinstr(compst, tt).i.code == ITestChar &&
                 getinstr(compst, tt).i.aux == c)
    addinstruction(compst, IAny, 0);
  else
    addinstruction(compst, IChar, c);
}


/*
** Add a charset posfix to an instruction
*/
static void addcharset (CompileState *compst, const byte *cs) {
  int p = gethere(compst);
  int i;
  for (i = 0; i < (int)CHARSETINSTSIZE - 1; i++)
    nextinstruction(compst);  /* space for buffer */
  /* fill buffer with charset */
  loopset(j, getinstr(compst, p).buff[j] = cs[j]);
}


/*
** code a char set, optimizing unit sets for IChar, "complete"
** sets for IAny, and empty sets for IFail; also use an IAny
** when instruction is dominated by an equivalent test.
*/
static void codecharset (CompileState *compst, const byte *cs, int tt) {
  int c = 0;  /* (=) to avoid warnings */
  Opcode op = charsettype(cs, &c);
  switch (op) {
    case IChar: codechar(compst, c, tt); break;
    case ISet: {  /* non-trivial set? */
      if (tt >= 0 && getinstr(compst, tt).i.code == ITestSet &&
          cs_equal(cs, getinstr(compst, tt + 2).buff))
        addinstruction(compst, IAny, 0);
      else {
        addinstruction(compst, ISet, 0);
        addcharset(compst, cs);
      }
      break;
    }
    default: addinstruction(compst, op, c); break;
  }
}


/*
** code a test set, optimizing unit sets for ITestChar, "complete"
** sets for ITestAny, and empty sets for IJmp (always fails).
** 'e' is true iff test should accept the empty string. (Test
** instructions in the current VM never accept the empty string.)
*/
static int codetestset (CompileState *compst, Charset *cs, int e) {
  if (e) return NOINST;  /* no test */
  else {
    int c = 0;
    Opcode op = charsettype(cs->cs, &c);
    switch (op) {
      case IFail: return addoffsetinst(compst, IJmp);  /* always jump */
      case IAny: return addoffsetinst(compst, ITestAny);
      case IChar: {
        int i = addoffsetinst(compst, ITestChar);
        getinstr(compst, i).i.aux = c;
        return i;
      }
      case ISet: {
        int i = addoffsetinst(compst, ITestSet);
        addcharset(compst, cs->cs);
        return i;
      }
      default: assert(0); return 0;
    }
  }
}


/*
** Find the final destination of a sequence of jumps
*/
static int finaltarget (Instruction *code, int i) {
  while (code[i].i.code == IJmp)
    i = target(code, i);
  return i;
}


/*
** final label (after traversing any jumps)
*/
static int finallabel (Instruction *code, int i) {
  return finaltarget(code, target(code, i));
}


/*
** <behind(p)> == behind n; <p>   (where n = fixedlen(p))
*/
static void codebehind (CompileState *compst, TTree *tree) {
  if (tree->u.n > 0)
    addinstruction(compst, IBehind, tree->u.n);
  codegen(compst, sib1(tree), 0, NOINST, fullset);
}


/*
** Choice; optimizations:
** - when p1 is headfail or
** when first(p1) and first(p2) are disjoint, than
** a character not in first(p1) cannot go to p1, and a character
** in first(p1) cannot go to p2 (at it is not in first(p2)).
** (The optimization is not valid if p1 accepts the empty string,
** as then there is no character at all...)
** - when p2 is empty and opt is true; a IPartialCommit can reuse
** the Choice already active in the stack.
*/
static void codechoice (CompileState *compst, TTree *p1, TTree *p2, int opt,
                        const Charset *fl) {
  int emptyp2 = (p2->tag == TTrue);
  Charset cs1, cs2;
  int e1 = getfirst(p1, fullset, &cs1);
  if (headfail(p1) ||
      (!e1 && (getfirst(p2, fl, &cs2), cs_disjoint(&cs1, &cs2)))) {
    /* <p1 / p2> == test (fail(p1)) -> L1 ; p1 ; jmp L2; L1: p2; L2: */
    int test = codetestset(compst, &cs1, 0);
    int jmp = NOINST;
    codegen(compst, p1, 0, test, fl);
    if (!emptyp2)
      jmp = addoffsetinst(compst, IJmp); 
    jumptohere(compst, test);
    codegen(compst, p2, opt, NOINST, fl);
    jumptohere(compst, jmp);
  }
  else if (opt && emptyp2) {
    /* p1? == IPartialCommit; p1 */
    jumptohere(compst, addoffsetinst(compst, IPartialCommit));
    codegen(compst, p1, 1, NOINST, fullset);
  }
  else {
    /* <p1 / p2> == 
        test(first(p1)) -> L1; choice L1; <p1>; commit L2; L1: <p2>; L2: */
    int pcommit;
    int test = codetestset(compst, &cs1, e1);
    int pchoice = addoffsetinst(compst, IChoice);
    codegen(compst, p1, emptyp2, test, fullset);
    pcommit = addoffsetinst(compst, ICommit);
    jumptohere(compst, pchoice);
    jumptohere(compst, test);
    codegen(compst, p2, opt, NOINST, fl);
    jumptohere(compst, pcommit);
  }
}


/*
** And predicate
** optimization: fixedlen(p) = n ==> <&p> == <p>; behind n
** (valid only when 'p' has no captures)
*/
static void codeand (CompileState *compst, TTree *tree, int tt) {
  int n = fixedlen(tree);
  if (n >= 0 && n <= MAXBEHIND && !hascaptures(tree)) {
    codegen(compst, tree, 0, tt, fullset);
    if (n > 0)
      addinstruction(compst, IBehind, n);
  }
  else {  /* default: Choice L1; p1; BackCommit L2; L1: Fail; L2: */
    int pcommit;
    int pchoice = addoffsetinst(compst, IChoice);
    codegen(compst, tree, 0, tt, fullset);
    pcommit = addoffsetinst(compst, IBackCommit);
    jumptohere(compst, pchoice);
    addinstruction(compst, IFail, 0);
    jumptohere(compst, pcommit);
  }
}


/*
** Captures: if pattern has fixed (and not too big) length, and it
** has no nested captures, use a single IFullCapture instruction
** after the match; otherwise, enclose the pattern with OpenCapture -
** CloseCapture.
*/
static void codecapture (CompileState *compst, TTree *tree, int tt,
                         const Charset *fl) {
  int len = fixedlen(sib1(tree));
  if (len >= 0 && len <= MAXOFF && !hascaptures(sib1(tree))) {
    codegen(compst, sib1(tree), 0, tt, fl);
    addinstcap(compst, IFullCapture, tree->cap, tree->key, len);
  }
  else {
    addinstcap(compst, IOpenCapture, tree->cap, tree->key, 0);
    codegen(compst, sib1(tree), 0, tt, fl);
    addinstcap(compst, ICloseCapture, Cclose, 0, 0);
  }
}


static void coderuntime (CompileState *compst, TTree *tree, int tt) {
  addinstcap(compst, IOpenCapture, Cgroup, tree->key, 0);
  codegen(compst, sib1(tree), 0, tt, fullset);
  addinstcap(compst, ICloseRunTime, Cclose, 0, 0);
}


/*
** Repetion; optimizations:
** When pattern is a charset, can use special instruction ISpan.
** When pattern is head fail, or if it starts with characters that
** are disjoint from what follows the repetions, a simple test
** is enough (a fail inside the repetition would backtrack to fail
** again in the following pattern, so there is no need for a choice).
** When 'opt' is true, the repetion can reuse the Choice already
** active in the stack.
*/
static void coderep (CompileState *compst, TTree *tree, int opt,
                     const Charset *fl) {
  Charset st;
  if (tocharset(tree, &st)) {
    addinstruction(compst, ISpan, 0);
    addcharset(compst, st.cs);
  }
  else {
    int e1 = getfirst(tree, fullset, &st);
    if (headfail(tree) || (!e1 && cs_disjoint(&st, fl))) {
      /* L1: test (fail(p1)) -> L2; <p>; jmp L1; L2: */
      int jmp;
      int test = codetestset(compst, &st, 0);
      codegen(compst, tree, 0, test, fullset);
      jmp = addoffsetinst(compst, IJmp);
      jumptohere(compst, test);
      jumptothere(compst, jmp, test);
    }
    else {
      /* test(fail(p1)) -> L2; choice L2; L1: <p>; partialcommit L1; L2: */
      /* or (if 'opt'): partialcommit L1; L1: <p>; partialcommit L1; */
      int commit, l2;
      int test = codetestset(compst, &st, e1);
      int pchoice = NOINST;
      if (opt)
        jumptohere(compst, addoffsetinst(compst, IPartialCommit));
      else
        pchoice = addoffsetinst(compst, IChoice);
      l2 = gethere(compst);
      codegen(compst, tree, 0, NOINST, fullset);
      commit = addoffsetinst(compst, IPartialCommit);
      jumptothere(compst, commit, l2);
      jumptohere(compst, pchoice);
      jumptohere(compst, test);
    }
  }
}


/*
** Not predicate; optimizations:
** In any case, if first test fails, 'not' succeeds, so it can jump to
** the end. If pattern is headfail, that is all (it cannot fail
** in other parts); this case includes 'not' of simple sets. Otherwise,
** use the default code (a choice plus a failtwice).
*/
static void codenot (CompileState *compst, TTree *tree) {
  Charset st;
  int e = getfirst(tree, fullset, &st);
  int test = codetestset(compst, &st, e);
  if (headfail(tree))  /* test (fail(p1)) -> L1; fail; L1:  */
    addinstruction(compst, IFail, 0);
  else {
    /* test(fail(p))-> L1; choice L1; <p>; failtwice; L1:  */
    int pchoice = addoffsetinst(compst, IChoice);
    codegen(compst, tree, 0, NOINST, fullset);
    addinstruction(compst, IFailTwice, 0);
    jumptohere(compst, pchoice);
  }
  jumptohere(compst, test);
}


/*
** change open calls to calls, using list 'positions' to find
** correct offsets; also optimize tail calls
*/
static void correctcalls (CompileState *compst, int *positions,
                          int from, int to) {
  int i;
  Instruction *code = compst->p->code;
  for (i = from; i < to; i += sizei(&code[i])) {
    if (code[i].i.code == IOpenCall) {
      int n = code[i].i.key;  /* rule number */
      int rule = positions[n];  /* rule position */
      assert(rule == from || code[rule - 1].i.code == IRet);
      if (code[finaltarget(code, i + 2)].i.code == IRet)  /* call; ret ? */
        code[i].i.code = IJmp;  /* tail call */
      else
        code[i].i.code = ICall;
      jumptothere(compst, i, rule);  /* call jumps to respective rule */
    }
  }
  assert(i == to);
}


/*
** Code for a grammar:
** call L1; jmp L2; L1: rule 1; ret; rule 2; ret; ...; L2:
*/
static void codegrammar (CompileState *compst, TTree *grammar) {
  int positions[MAXRULES];
  int rulenumber = 0;
  TTree *rule;
  int firstcall = addoffsetinst(compst, ICall);  /* call initial rule */
  int jumptoend = addoffsetinst(compst, IJmp);  /* jump to the end */
  int start = gethere(compst);  /* here starts the initial rule */
  jumptohere(compst, firstcall);
  for (rule = sib1(grammar); rule->tag == TRule; rule = sib2(rule)) {
    positions[rulenumber++] = gethere(compst);  /* save rule position */
    codegen(compst, sib1(rule), 0, NOINST, fullset);  /* code rule */
    addinstruction(compst, IRet, 0);
  }
  assert(rule->tag == TTrue);
  jumptohere(compst, jumptoend);
  correctcalls(compst, positions, start, gethere(compst));
}


static void codecall (CompileState *compst, TTree *call) {
  int c = addoffsetinst(compst, IOpenCall);  /* to be corrected later */
  getinstr(compst, c).i.key = sib2(call)->cap;  /* rule number */
  assert(sib2(call)->tag == TRule);
}


/*
** Code first child of a sequence
** (second child is called in-place to allow tail call)
** Return 'tt' for second child
*/
static int codeseq1 (CompileState *compst, TTree *p1, TTree *p2,
                     int tt, const Charset *fl) {
  if (needfollow(p1)) {
    Charset fl1;
    getfirst(p2, fl, &fl1);  /* p1 follow is p2 first */
    codegen(compst, p1, 0, tt, &fl1);
  }
  else  /* use 'fullset' as follow */
    codegen(compst, p1, 0, tt, fullset);
  if (fixedlen(p1) != 0)  /* can 'p1' consume anything? */
    return  NOINST;  /* invalidate test */
  else return tt;  /* else 'tt' still protects sib2 */
}


/*
** Main code-generation function: dispatch to auxiliar functions
** according to kind of tree. ('needfollow' should return true
** only for consructions that use 'fl'.)
*/
static void codegen (CompileState *compst, TTree *tree, int opt, int tt,
                     const Charset *fl) {
 tailcall:
  switch (tree->tag) {
    case TChar: codechar(compst, tree->u.n, tt); break;
    case TAny: addinstruction(compst, IAny, 0); break;
    case TSet: codecharset(compst, treebuffer(tree), tt); break;
    case TTrue: break;
    case TFalse: addinstruction(compst, IFail, 0); break;
    case TChoice: codechoice(compst, sib1(tree), sib2(tree), opt, fl); break;
    case TRep: coderep(compst, sib1(tree), opt, fl); break;
    case TBehind: codebehind(compst, tree); break;
    case TNot: codenot(compst, sib1(tree)); break;
    case TAnd: codeand(compst, sib1(tree), tt); break;
    case TCapture: codecapture(compst, tree, tt, fl); break;
    case TRunTime: coderuntime(compst, tree, tt); break;
    case TGrammar: codegrammar(compst, tree); break;
    case TCall: codecall(compst, tree); break;
    case TSeq: {
      tt = codeseq1(compst, sib1(tree), sib2(tree), tt, fl);  /* code 'p1' */
      /* codegen(compst, p2, opt, tt, fl); */
      tree = sib2(tree); goto tailcall;
    }
    default: assert(0);
  }
}


/*
** Optimize jumps and other jump-like instructions.
** * Update labels of instructions with labels to their final
** destinations (e.g., choice L1; ... L1: jmp L2: becomes
** choice L2)
** * Jumps to other instructions that do jumps become those
** instructions (e.g., jump to return becomes a return; jump
** to commit becomes a commit)
*/
static void peephole (CompileState *compst) {
  Instruction *code = compst->p->code;
  int i;
  for (i = 0; i < compst->ncode; i += sizei(&code[i])) {
   redo:
    switch (code[i].i.code) {
      case IChoice: case ICall: case ICommit: case IPartialCommit:
      case IBackCommit: case ITestChar: case ITestSet:
      case ITestAny: {  /* instructions with labels */
        jumptothere(compst, i, finallabel(code, i));  /* optimize label */
        break;
      }
      case IJmp: {
        int ft = finaltarget(code, i);
        switch (code[ft].i.code) {  /* jumping to what? */
          case IRet: case IFail: case IFailTwice:
          case IEnd: {  /* instructions with unconditional implicit jumps */
            code[i] = code[ft];  /* jump becomes that instruction */
            code[i + 1].i.code = IAny;  /* 'no-op' for target position */
            break;
          }
          case ICommit: case IPartialCommit:
          case IBackCommit: {  /* inst. with unconditional explicit jumps */
            int fft = finallabel(code, ft);
            code[i] = code[ft];  /* jump becomes that instruction... */
            jumptothere(compst, i, fft);  /* but must correct its offset */
            goto redo;  /* reoptimize its label */
          }
          default: {
            jumptothere(compst, i, ft);  /* optimize label */
            break;
          }
        }
        break;
      }
      default: break;
    }
  }
  assert(code[i - 1].i.code == IEnd);
}


/*
** Compile a pattern
*/
Instruction *compile (lua_State *L, Pattern *p) {
  CompileState compst;
  compst.p = p;  compst.ncode = 0;  compst.L = L;
  realloccode(L, p, 2);  /* minimum initial size */
  codegen(&compst, p->tree, 0, NOINST, fullset);
  addinstruction(&compst, IEnd, 0);
  realloccode(L, p, compst.ncode);  /* set final size */
  peephole(&compst);
  return p->code;
}


/* }====================================================== */

