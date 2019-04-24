/*
** $Id: lstring.c,v 2.56.1.1 2017/04/19 17:20:42 roberto Exp $
** String table (keeps all strings handled by Lua)
** See Copyright Notice in lua.h
*/

#define lstring_c
#define LUA_CORE

#include "lprefix.h"


#include <string.h>
#include <time.h>
#include "lua.h"

#include "ldebug.h"
#include "ldo.h"
#include "lmem.h"
#include "lobject.h"
#include "lstate.h"
#include "lstring.h"


#define MEMERRMSG       "not enough memory"


/*
** Lua will use at most ~(2^LUAI_HASHLIMIT) bytes from a string to
** compute its hash
*/
#if !defined(LUAI_HASHLIMIT)
#define LUAI_HASHLIMIT		5
#endif


/*
** equality for long strings
*/
int luaS_eqlngstr (TString *a, TString *b) {
  size_t len = a->u.lnglen;
  lua_assert(a->tt == LUA_TLNGSTR && b->tt == LUA_TLNGSTR);
  return (a == b) ||  /* same instance or... */
    ((len == b->u.lnglen) &&  /* equal length and ... */
     (memcmp(getstr(a), getstr(b), len) == 0));  /* equal contents */
}


unsigned int luaS_hash (const char *str, size_t l, unsigned int seed) {
  unsigned int h = seed ^ cast(unsigned int, l);
  size_t step = (l >> LUAI_HASHLIMIT) + 1;
  for (; l >= step; l -= step)
    h ^= ((h<<5) + (h>>2) + cast_byte(str[l - 1]));
  return h;
}


unsigned int luaS_hashlongstr (TString *ts) {
  lua_assert(ts->tt == LUA_TLNGSTR);
  if (ts->extra == 0) {  /* no hash? */
    ts->hash = luaS_hash(getstr(ts), ts->u.lnglen, ts->hash);
    ts->extra = 1;  /* now it has its hash */
  }
  return ts->hash;
}


/*
** resizes the string table
*/
void luaS_resize (lua_State *L, int newsize) {
  int i;
  stringtable *tb = &G(L)->strt;
  if (newsize > tb->size) {  /* grow table if needed */
    luaM_reallocvector(L, tb->hash, tb->size, newsize, TString *);
    for (i = tb->size; i < newsize; i++)
      tb->hash[i] = NULL;
  }
  for (i = 0; i < tb->size; i++) {  /* rehash */
    TString *p = tb->hash[i];
    tb->hash[i] = NULL;
    while (p) {  /* for each node in the list */
      TString *hnext = p->u.hnext;  /* save next */
      unsigned int h = lmod(p->hash, newsize);  /* new position */
      p->u.hnext = tb->hash[h];  /* chain it */
      tb->hash[h] = p;
      p = hnext;
    }
  }
  if (newsize < tb->size) {  /* shrink table if needed */
    /* vanishing slice should be empty */
    lua_assert(tb->hash[newsize] == NULL && tb->hash[tb->size - 1] == NULL);
    luaM_reallocvector(L, tb->hash, tb->size, newsize, TString *);
  }
  tb->size = newsize;
}


/*
** Clear API string cache. (Entries cannot be empty, so fill them with
** a non-collectable string.)
*/
void luaS_clearcache (global_State *g) {
  int i, j;
  for (i = 0; i < STRCACHE_N; i++)
    for (j = 0; j < STRCACHE_M; j++) {
    if (iswhite(g->strcache[i][j]))  /* will entry be collected? */
      g->strcache[i][j] = g->memerrmsg;  /* replace it with something fixed */
    }
}


/*
** Initialize the string table and the string cache
*/
void luaS_init (lua_State *L) {
  global_State *g = G(L);
  int i, j;
  luaS_resize(L, MINSTRTABSIZE);  /* initial size of string table */
  /* pre-create memory-error message */
  g->memerrmsg = luaS_newliteral(L, MEMERRMSG);
  luaC_fix(L, obj2gco(g->memerrmsg));  /* it should never be collected */
  for (i = 0; i < STRCACHE_N; i++)  /* fill cache with valid strings */
    for (j = 0; j < STRCACHE_M; j++)
      g->strcache[i][j] = g->memerrmsg;
}



/*
** creates a new string object
*/
static TString *createstrobj (lua_State *L, size_t l, int tag, unsigned int h) {
  TString *ts;
  GCObject *o;
  size_t totalsize;  /* total size of TString object */
  totalsize = sizelstring(l);
  o = luaC_newobj(L, tag, totalsize);
  ts = gco2ts(o);
  ts->hash = h;
  ts->extra = 0;
  getstr(ts)[l] = '\0';  /* ending 0 */
  return ts;
}

static unsigned int LNGSTR_SEED;
static unsigned int make_lstr_seed() {
  size_t buff[4];
  unsigned int h = time(NULL);
  buff[0] = cast(size_t, h);
  buff[1] = cast(size_t, &LNGSTR_SEED);
  buff[2] = cast(size_t, &make_lstr_seed);
  buff[3] = cast(size_t, &luaS_createlngstrobj);
  return luaS_hash((const char*)buff, sizeof(buff), h);
}

TString *luaS_createlngstrobj (lua_State *L, size_t l) {
  TString *ts = createstrobj(L, l, LUA_TLNGSTR, LNGSTR_SEED);
  ts->u.lnglen = l;
  return ts;
}


void luaS_remove (lua_State *L, TString *ts) {
  stringtable *tb = &G(L)->strt;
  TString **p = &tb->hash[lmod(ts->hash, tb->size)];
  while (*p != ts)  /* find previous element */
    p = &(*p)->u.hnext;
  *p = (*p)->u.hnext;  /* remove element from its list */
  tb->nuse--;
}


/*
** checks whether short string exists and reuses it or creates a new one
*/
static TString *queryshrstr (lua_State *L, const char *str, size_t l, unsigned int h) {
  TString *ts;
  global_State *g = G(L);
  TString **list = &g->strt.hash[lmod(h, g->strt.size)];
  lua_assert(str != NULL);  /* otherwise 'memcmp'/'memcpy' are undefined */
  for (ts = *list; ts != NULL; ts = ts->u.hnext) {
    if (l == ts->shrlen &&
        (memcmp(str, getstr(ts), l * sizeof(char)) == 0)) {
      /* found! */
      if (isdead(g, ts))  /* dead (but not collected yet)? */
        changewhite(ts);  /* resurrect it */
      return ts;
    }
  }
  return NULL;
}

static TString *addshrstr (lua_State *L, const char *str, size_t l, unsigned int h) {
  TString *ts;
  global_State *g = G(L);
  TString **list = &g->strt.hash[lmod(h, g->strt.size)];
  if (g->strt.nuse >= g->strt.size && g->strt.size <= MAX_INT/2) {
    luaS_resize(L, g->strt.size * 2);
    list = &g->strt.hash[lmod(h, g->strt.size)];  /* recompute with new size */
  }
  ts = createstrobj(L, l, LUA_TSHRSTR, h);
  memcpy(getstr(ts), str, l * sizeof(char));
  ts->shrlen = cast_byte(l);
  ts->u.hnext = *list;
  *list = ts;
  g->strt.nuse++;
  return ts;
}

static TString *internshrstr (lua_State *L, const char *str, size_t l);

/*
** new string (with explicit length)
*/
TString *luaS_newlstr (lua_State *L, const char *str, size_t l) {
  if (l <= LUAI_MAXSHORTLEN)  /* short string? */
    return internshrstr(L, str, l);
  else {
    TString *ts;
    if (l >= (MAX_SIZE - sizeof(TString))/sizeof(char))
      luaM_toobig(L);
    ts = luaS_createlngstrobj(L, l);
    memcpy(getstr(ts), str, l * sizeof(char));
    return ts;
  }
}


/*
** Create or reuse a zero-terminated string, first checking in the
** cache (using the string address as a key). The cache can contain
** only zero-terminated strings, so it is safe to use 'strcmp' to
** check hits.
*/
TString *luaS_new (lua_State *L, const char *str) {
  unsigned int i = point2uint(str) % STRCACHE_N;  /* hash */
  int j;
  TString **p = G(L)->strcache[i];
  for (j = 0; j < STRCACHE_M; j++) {
    if (strcmp(str, getstr(p[j])) == 0)  /* hit? */
      return p[j];  /* that is it */
  }
  /* normal route */
  for (j = STRCACHE_M - 1; j > 0; j--)
    p[j] = p[j - 1];  /* move out last element */
  /* new element is first in the list */
  p[0] = luaS_newlstr(L, str, strlen(str));
  return p[0];
}


Udata *luaS_newudata (lua_State *L, size_t s) {
  Udata *u;
  GCObject *o;
  if (s > MAX_SIZE - sizeof(Udata))
    luaM_toobig(L);
  o = luaC_newobj(L, LUA_TUSERDATA, sizeludata(s));
  u = gco2u(o);
  u->len = s;
  u->metatable = NULL;
  setuservalue(L, u, luaO_nilobject);
  return u;
}

/*
 * global shared table
 */

#include "rwlock.h"
#include "atomic.h"
#include <stdlib.h>

#define getaddrstr(ts)	(cast(char *, (ts)) + sizeof(UTString))
#define SHRSTR_INITSIZE 0x10000

struct shrmap_slot {
	struct rwlock lock;
	TString *str;
};

struct shrmap {
	struct rwlock lock;
	int n;
	int mask;
	int total;
	int roslots;
	struct shrmap_slot * readwrite;
	struct shrmap_slot * readonly;
};

static struct shrmap SSM;

static struct shrmap_slot *
shrstr_newpage(int sz) {
	int i;
	struct shrmap_slot * s = (struct shrmap_slot *)malloc(sz * sizeof(*s));
	if (s == NULL)
		return NULL;
	for (i=0;i<sz;i++) {
		rwlock_init(&s[i].lock);
		s[i].str = NULL;
	}
	return s;
}

static void
shrstr_deletepage(struct shrmap_slot *s, int sz) {
	if (s) {
		int i;
		for (i=0;i<sz;i++) {
			TString *str = s[i].str;
			while (str) {
				TString * next = str->u.hnext;
				free(str);
				str = next;
			}
		}
		free(s);
	}
}

static int
shrstr_allocpage(struct shrmap * s, int osz, int sz, struct shrmap_slot * newpage) {
	if (s->readonly != NULL)
		return 0;
	if ((s->mask + 1) != osz)
		return 0;
	s->readonly = s->readwrite;
	s->readwrite = newpage;
	s->roslots = s->mask + 1;
	s->mask = sz - 1;

	return 1;
}

static void
shrstr_rehash(struct shrmap *s, int slotid) {
	struct shrmap_slot *slot = &s->readonly[slotid];
	rwlock_wlock(&slot->lock);
		TString *str = slot->str;
		while (str) {
			TString * next = str->u.hnext;
			int newslotid = str->hash & s->mask;
			struct shrmap_slot *newslot = &s->readwrite[newslotid];
			rwlock_wlock(&newslot->lock);
				str->u.hnext = newslot->str;
				newslot->str = str;
			rwlock_wunlock(&newslot->lock);
			str = next;
		}

		slot->str = NULL;
	rwlock_wunlock(&slot->lock);
}

/*
	1. writelock SSM if readonly == NULL, (Only one thread can expand)
	2. move old page (readwrite) to readonly
	3. new (empty) page with double size to readwrite
	4. unlock SSM
	5. rehash every slots
	6. remove temporary readonly (writelock SSM)
 */
static void
shrstr_expandpage(int cap) {
	struct shrmap * s = &SSM;
	if (s->readonly)
		return;
	int osz = s->mask + 1;
	int sz = osz * 2;
	while (sz < cap) {
		// overflow check
		if (sz <= 0)
			return;
		sz = sz * 2;
	}
	struct shrmap_slot * newpage = shrstr_newpage(sz);
	if (newpage == NULL)
		return;
	rwlock_wlock(&s->lock);
	int succ = shrstr_allocpage(s, osz, sz, newpage);
	rwlock_wunlock(&s->lock);
	if (!succ) {
		shrstr_deletepage(newpage, sz);
		return;
	}
	int i;
	for (i=0;i<osz;i++) {
		shrstr_rehash(s, i);
	}
	rwlock_wlock(&s->lock);
		struct shrmap_slot * oldpage = s->readonly;
		s->readonly = NULL;
	rwlock_wunlock(&s->lock);
	shrstr_deletepage(oldpage, osz);
}

LUA_API void
luaS_initshr() {
	struct shrmap * s = &SSM;
	rwlock_init(&s->lock);
	s->n = 0;
	s->mask = SHRSTR_INITSIZE - 1;
	s->readwrite = shrstr_newpage(SHRSTR_INITSIZE);
	s->readonly = NULL;
	LNGSTR_SEED = make_lstr_seed();
}

LUA_API void
luaS_exitshr() {
	struct shrmap * s = &SSM;
	rwlock_wlock(&s->lock);
	int sz = s->mask + 1;
	shrstr_deletepage(s->readwrite, sz);
	shrstr_deletepage(s->readonly, s->roslots);
	s->readwrite = NULL;
	s->readonly = NULL;
}

static TString *
find_string(TString *t, struct shrmap_slot * slot, unsigned int h, const char *str, lu_byte l) {
	TString *ts = slot->str;
	if (t) {
		while (ts) {
			if (ts == t)
				break;
			ts = ts->u.hnext;
		}
	} else {
		while (ts) {
			if (ts->hash == h &&
				ts->shrlen == l &&
				memcmp(str, ts+1, l) == 0) {
				break;
			}
			ts = ts->u.hnext;
		}
	}
	return ts;
}

/*
	1. readlock SSM
	2. find string in readwrite page
	3. find string in readonly (if exist, during exapnding)
	4. unlock SSM
 */
static TString *
query_string(TString *t, unsigned int h, const char *str, lu_byte l) {
	struct shrmap * s = &SSM;
	TString *ts = NULL;
	rwlock_rlock(&s->lock);
		struct shrmap_slot *slot = &s->readwrite[h & s->mask];
		rwlock_rlock(&slot->lock);
			ts = find_string(t, slot, h, str, l);
		rwlock_runlock(&slot->lock);
		if (ts == NULL && s->readonly != NULL) {
			int mask = s->roslots - 1;
			slot = &s->readonly[h & mask];
			rwlock_rlock(&slot->lock);
				ts = find_string(t, slot, h, str, l);
			rwlock_runlock(&slot->lock);
		}
	rwlock_runlock(&s->lock);
	return ts;
}

static TString *
new_string(unsigned int h, const char *str, lu_byte l) {
	size_t sz = sizelstring(l);
	TString *ts = malloc(sz);
	memset(ts, 0, sz);
	ts->tt = LUA_TSHRSTR;
	ts->hash = h;
	ts->shrlen = l;
	memcpy(ts+1, str, l);
	return ts;
}

static TString *
shrstr_exist(struct shrmap * s, unsigned int h, const char *str, lu_byte l) {
	TString *found;
	if (s->readonly) {
		unsigned int mask = s->roslots - 1;
		struct shrmap_slot *slot = &s->readonly[h & mask];
		rwlock_rlock(&slot->lock);
			found = find_string(NULL, slot, h, str, l);
		rwlock_runlock(&slot->lock);
		if (found)
			return found;
	}
	struct shrmap_slot *slot = &s->readwrite[h & s->mask];
	rwlock_wlock(&slot->lock);
	found = find_string(NULL, slot, h, str, l);
	if (found) {
		rwlock_wunlock(&slot->lock);
		return found;
	}
	// not found, lock slot and return.
	return NULL;
}

static TString *
add_string(unsigned int h, const char *str, lu_byte l) {
	struct shrmap * s = &SSM;
	TString * tmp = new_string(h, str, l);
	rwlock_rlock(&s->lock);
		struct TString *ts = shrstr_exist(s, h, str, l);
		if (ts) {
			// string is create by other thread, so free tmp
			free(tmp);
		} else {
			struct shrmap_slot *slot = &s->readwrite[h & s->mask];
			ts = tmp;
			ts->u.hnext = slot->str;
			slot->str = ts;
			rwlock_wunlock(&slot->lock);
			ATOM_INC(&SSM.total);
		}
	rwlock_runlock(&s->lock);
	return ts;
}

static TString *
internshrstr (lua_State *L, const char *str, size_t l) {
  TString *ts;
  global_State *g = G(L);
  unsigned int h = luaS_hash(str, l, g->seed);
  unsigned int h0;
  // lookup global state of this L first
  ts = queryshrstr (L, str, l, h);
  if (ts)
    return ts;
  // lookup SSM again
  h0 = luaS_hash(str, l, 0);
  ts = query_string(NULL, h0, str, l);
  if (ts)
    return ts;
  // If SSM.n greate than 0, add it to SSM
  if (SSM.n > 0) {
    ATOM_DEC(&SSM.n);
    return add_string(h0, str, l);
  }
  // Else add it to global state (local)
  return addshrstr (L, str, l, h);
}

LUA_API void
luaS_expandshr(int n) {
	struct shrmap * s = &SSM;
	if (n < 0) {
		if (-n > s->n) {
			n = -s->n;
		}
	}
	ATOM_ADD(&s->n, n);
	if (n > 0) {
		int t = (s->total + s->n) * 5 / 4;
		if (t > s->mask) {
			shrstr_expandpage(t);
		}
	}
}

LUAI_FUNC TString *
luaS_clonestring(lua_State *L, TString *ts) {
  unsigned int h;
  int l;
  const char * str = getaddrstr(ts);
  global_State *g = G(L);
  TString *result;
  if (ts->tt == LUA_TLNGSTR)
    return luaS_newlstr(L, str, ts->u.lnglen);
  // look up global state of this L first
  l = ts->shrlen;
  h = luaS_hash(str, l, g->seed);
  result = queryshrstr (L, str, l, h);
  if (result)
    return result;
  // look up SSM by ptr
  result = query_string(ts, ts->hash, NULL, 0);
  if (result)
    return result;
  h = luaS_hash(str, l, 0);
  result = query_string(NULL, h, str, l);
  if (result)
    return result;
  // ts is not in SSM, so recalc hash, and add it to SSM
  return add_string(h, str, l);
}

struct slotinfo {
	int len;
	int size;
};

static void
getslot(struct shrmap_slot *s, struct slotinfo *info) {
	memset(info, 0, sizeof(*info));
	rwlock_rlock(&s->lock);
	TString *ts = s->str;
	while (ts) {
		++info->len;
		info->size += sizelstring(ts->shrlen);
		ts = ts->u.hnext;
	}
	rwlock_runlock(&s->lock);
}


struct variance {
	int count;
	double mean;
	double m2;
};

static void
variance_update(struct variance *v, int newValue_) {
	double newValue = (double)newValue_;
	++v->count;
	double delta = newValue - v->mean;
	v->mean += delta / v->count;
	double delta2 = newValue - v->mean;
	v->m2 += delta * delta2;
}

LUA_API int
luaS_shrinfo(lua_State *L) {
	struct slotinfo total;
	struct slotinfo tmp;
	memset(&total, 0, sizeof(total));
	struct shrmap * s = &SSM;
	struct variance v = { 0,0,0 };
	int slots = 0;
	rwlock_rlock(&s->lock);
		int i;
		int sz = s->mask + 1;
		for (i=0;i<sz;i++) {
			struct shrmap_slot *slot = &s->readwrite[i];
			getslot(slot, &tmp);
			if (tmp.len > 0) {
				if (tmp.len > total.len) {
					total.len = tmp.len;
				}
				total.size += tmp.size;
				variance_update(&v, tmp.len);
				++slots;
			}
		}
		if (s->readonly) {
			sz = s->roslots;
			for (i=0;i<sz;i++) {
				struct shrmap_slot *slot = &s->readonly[i];
				getslot(slot, &tmp);
				if (tmp.len > 0) {
					if (tmp.len > total.len) {
						total.len = tmp.len;
					}
					total.size += tmp.size;
					variance_update(&v, tmp.len);
				}
			}
		}
	rwlock_runlock(&s->lock);
	lua_pushinteger(L, SSM.total);	// count
	lua_pushinteger(L, total.size);	// total size
	lua_pushinteger(L, total.len);	// longest
	lua_pushinteger(L, SSM.n);	// space
	lua_pushinteger(L, slots);	// slots
	if (v.count > 1) {
		lua_pushnumber(L, v.m2 / v.count);	// variance
	} else {
		lua_pushnumber(L, 0);	// variance
	}
	return 6;
}
