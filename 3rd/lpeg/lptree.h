/*  
** $Id: lptree.h,v 1.2 2013/03/24 13:51:12 roberto Exp $
*/

#if !defined(lptree_h)
#define lptree_h


#include "lptypes.h" 


/*
** types of trees
*/
typedef enum TTag {
  TChar = 0, TSet, TAny,  /* standard PEG elements */
  TTrue, TFalse,
  TRep,
  TSeq, TChoice,
  TNot, TAnd,
  TCall,
  TOpenCall,
  TRule,  /* sib1 is rule's pattern, sib2 is 'next' rule */
  TGrammar,  /* sib1 is initial (and first) rule */
  TBehind,  /* match behind */
  TCapture,  /* regular capture */
  TRunTime  /* run-time capture */
} TTag;

/* number of siblings for each tree */
extern const byte numsiblings[];


/*
** Tree trees
** The first sibling of a tree (if there is one) is immediately after
** the tree.  A reference to a second sibling (ps) is its position
** relative to the position of the tree itself.  A key in ktable
** uses the (unique) address of the original tree that created that
** entry. NULL means no data.
*/
typedef struct TTree {
  byte tag;
  byte cap;  /* kind of capture (if it is a capture) */
  unsigned short key;  /* key in ktable for Lua data (0 if no key) */
  union {
    int ps;  /* occasional second sibling */
    int n;  /* occasional counter */
  } u;
} TTree;


/*
** A complete pattern has its tree plus, if already compiled,
** its corresponding code
*/
typedef struct Pattern {
  union Instruction *code;
  int codesize;
  TTree tree[1];
} Pattern;


/* number of siblings for each tree */
extern const byte numsiblings[];

/* access to siblings */
#define sib1(t)         ((t) + 1)
#define sib2(t)         ((t) + (t)->u.ps)






#endif

