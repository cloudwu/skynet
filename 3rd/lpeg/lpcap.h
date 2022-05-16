/*
** $Id: lpcap.h $
*/

#if !defined(lpcap_h)
#define lpcap_h


#include "lptypes.h"


/* kinds of captures */
typedef enum CapKind {
  Cclose,  /* not used in trees */
  Cposition,
  Cconst,  /* ktable[key] is Lua constant */
  Cbackref,  /* ktable[key] is "name" of group to get capture */
  Carg,  /* 'key' is arg's number */
  Csimple,  /* next node is pattern */
  Ctable,  /* next node is pattern */
  Cfunction,  /* ktable[key] is function; next node is pattern */
  Cquery,  /* ktable[key] is table; next node is pattern */
  Cstring,  /* ktable[key] is string; next node is pattern */
  Cnum,  /* numbered capture; 'key' is number of value to return */
  Csubst,  /* substitution capture; next node is pattern */
  Cfold,  /* ktable[key] is function; next node is pattern */
  Cruntime,  /* not used in trees (is uses another type for tree) */
  Cgroup  /* ktable[key] is group's "name" */
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
  int reclevel;  /* recursion level */
} CapState;


int runtimecap (CapState *cs, Capture *close, const char *s, int *rem);
int getcaptures (lua_State *L, const char *s, const char *r, int ptop);
int finddyncap (Capture *cap, Capture *last);

#endif


