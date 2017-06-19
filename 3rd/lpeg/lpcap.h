/*
** $Id: lpcap.h,v 1.2 2015/02/27 17:13:17 roberto Exp $
*/

#if !defined(lpcap_h)
#define lpcap_h


#include "lptypes.h"


/* kinds of captures */
typedef enum CapKind {
  Cclose, Cposition, Cconst, Cbackref, Carg, Csimple, Ctable, Cfunction,
  Cquery, Cstring, Cnum, Csubst, Cfold, Cruntime, Cgroup
} CapKind;


typedef struct Capture {
  const char *s;  /* subject position */
  unsigned short idx;  /* extra info (group name, arg index, etc.) */
  byte kind;  /* kind of capture */
  byte siz;  /* size of full capture + 1 (0 = not a full capture) */
} Capture;


typedef struct CapState {
  Capture *cap;  /* current capture */
  Capture *ocap;  /* (original) capture list */
  lua_State *L;
  int ptop;  /* index of last argument to 'match' */
  const char *s;  /* original string */
  int valuecached;  /* value stored in cache slot */
} CapState;


int runtimecap (CapState *cs, Capture *close, const char *s, int *rem);
int getcaptures (lua_State *L, const char *s, const char *r, int ptop);
int finddyncap (Capture *cap, Capture *last);

#endif


