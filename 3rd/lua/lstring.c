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

static unsigned int STRSEED;

#define STRFIXSIZE 64

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
** Clear API string cache. (Entries cannot be empty, so fill them with
** a non-collectable string.)
*/
void luaS_clearcache (global_State *g) {
  int i, j;
  for (i = 0; i < STRCACHE_N; i++)
    for (j = 0; j < STRCACHE_M; j++) {
    if (!isshared(g->strcache[i][j]) && iswhite(g->strcache[i][j]))  /* will entry be collected? */
      g->strcache[i][j] = g->memerrmsg;  /* replace it with something fixed */
    }
}

static struct ssm_ref * newref(int size);

/*
** Initialize the string table and the string cache
*/
void luaS_init (lua_State *L) {
  global_State *g = G(L);
  int i, j;
  g->strsave = newref(MINSTRTABSIZE);
  g->strmark = newref(MINSTRTABSIZE);
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

TString *luaS_createlngstrobj (lua_State *L, size_t l) {
  TString *ts = createstrobj(L, l, LUA_TLNGSTR, STRSEED);
  ts->u.lnglen = l;
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
#include "spinlock.h"
#include "atomic.h"
#include <stdlib.h>
#include <string.h>

#define SHRSTR_INITSIZE 0x10000

/* prime is better for hash */
#define VMHASHSLOTS 4093

struct shrmap_slot {
	struct rwlock lock;
	TString *str;
};

struct ssm_ref {
	TString **hash;
	TString **array;
	int nuse;  /* number of elements */
	int hsize;
	int asize;
	int acap;
};

struct collect_queue {
	struct collect_queue *next;
	void * key;
	struct ssm_ref *strsave;
	struct ssm_ref *strmark;
	struct ssm_ref *strfix;
};

struct shrmap {
	struct rwlock lock;
	int rwslots;
	int total;
	int garbage;
	int roslots;
	struct shrmap_slot * readwrite;
	struct shrmap_slot * readonly;
	struct spinlock qlock;
	struct collect_queue * head;
	struct collect_queue * tail;
	struct collect_queue * vm[VMHASHSLOTS];
};

static struct shrmap SSM;

#define ADD_SREF(ts) do {if(ATOM_INC(&((ts)->u.ref))==1) ATOM_DEC(&SSM.garbage);}while(0)
#define DEC_SREF(ts) do {if(ATOM_DEC(&((ts)->u.ref))==0) ATOM_INC(&SSM.garbage);}while(0)
#define ZERO_SREF(ts) ((ts)->u.ref == 0)
#define FREE_SREF(ts) do {free(ts);ATOM_DEC(&SSM.total);ATOM_DEC(&SSM.garbage);}while(0)

static struct ssm_ref *
newref(int size) {
	/* size must be must be power of 2 */
	lua_assert( (size&(size-1))==0 );
	struct ssm_ref *r = (struct ssm_ref *)malloc(sizeof(*r));
	if (r == NULL)
		return NULL;
	TString **hash = (TString **)malloc(sizeof(TString *) * size);
	if (hash == NULL) {
		free(r);
		return NULL;
	}
	memset(r, 0, sizeof(*r));
	memset(hash, 0, sizeof(TString *) * size);
	r->hsize = size;
	r->hash = hash;
	return r;
}

static void
expand_ref(struct ssm_ref *r, int changeref) {
	int hsize = r->hsize * 2;
	TString ** hash = (TString **)malloc(sizeof(TString *) * hsize);
	if (hash == NULL)
		return;
	memset(hash, 0, sizeof(TString *) * hsize);
	int i;
	for (i=0;i<r->hsize;i++) {
		TString *s = r->hash[i];
		if (s) {
			hash[lmod(s->hash, hsize)] = s;
		}
	}
	free(r->hash);
	r->hash = hash;
	r->hsize = hsize;

	for (i=0;i<r->asize;) {
		TString *s = r->array[i];
		int slot = lmod(s->hash, hsize);
		TString *hs = hash[slot];
		if (hs == s || hs == NULL) {
			if (hs == NULL)
				hash[slot] = s;
			else {
				--r->nuse;
				if (changeref)
					DEC_SREF(s);
			}
			--r->asize;
			r->array[i] = r->array[r->asize];
		} else {
			++i;
		}
	}
}

static void
insert_ref(struct ssm_ref *r, TString *s) {
	if (r->asize >= r->acap) {
		r->acap = r->asize * 2;
		if (r->acap == 0) {
			r->acap = r->hsize / 2;
		}
		TString ** array = (TString **)realloc(r->array, r->acap * sizeof(TString *));
		lua_assert(array != NULL);
		r->array = array;
	}
	r->array[r->asize++] = s;
}

static void
shrink_ref(struct ssm_ref *r) {
	int hsize = r->hsize / 2;
	if (hsize < MINSTRTABSIZE)
		return;
	TString ** hash = (TString **)malloc(sizeof(TString *) * hsize);
	if (hash == NULL)
		return;
	memset(hash, 0, sizeof(TString *) * hsize);
	int i;
	for (i=0;i<r->hsize;i++) {
		TString *s = r->hash[i];
		if (s) {
			int h = lmod(s->hash, hsize);
			if (hash[h] == NULL)
				hash[h] = s;
			else
				insert_ref(r, s);
		}
	}
	free(r->hash);
	r->hash = hash;
	r->hsize = hsize;
}

static void
markref(struct ssm_ref *r, TString *s, int changeref) {
	unsigned int h = s->hash;
	int slot = lmod(h, r->hsize);
	TString * hs = r->hash[slot];
	if (hs == s){
		if (changeref)
			DEC_SREF(s);
		return;
	}
	++r->nuse;
	if (r->nuse >= r->hsize && r->hsize <= MAX_INT/2) {
		expand_ref(r, changeref);
		slot = lmod(h, r->hsize);
		hs = r->hash[slot];
	}
	if (hs != NULL) {
		if (hs == s) {
			--r->nuse;
			if (changeref)
				DEC_SREF(s);
			return;
		}
		insert_ref(r, hs);
	}
	r->hash[slot] = s;
}

void
luaS_mark(global_State *g, TString *s) {
	markref(g->strmark, s, 0);
}

void
luaS_fix(global_State *g, TString *s) {
	if (g->strfix == NULL)
		g->strfix = newref(STRFIXSIZE);
	markref(g->strfix, s, 0);
}

static void
delete_ref(struct ssm_ref *r) {
	if (r == NULL)
		return;
	free(r->hash);
	free(r->array);
	free(r);
}

static void
delete_cqueue(struct collect_queue *cqueue) {
	delete_ref(cqueue->strsave);
	delete_ref(cqueue->strmark);
	delete_ref(cqueue->strfix);
	free(cqueue);
}

static void
free_cqueue(struct collect_queue *cqueue) {
	while (cqueue) {
		struct collect_queue * next = cqueue->next;
		delete_cqueue(cqueue);
		cqueue = next;
	}
}

static void
remove_duplicate(struct ssm_ref *r, int decref) {
	int i = 0;
	while (i < r->asize) {
		TString *s = r->array[i];
		if (r->hash[lmod(s->hash, r->hsize)] == s) {
			--r->nuse;
			--r->asize;
			r->array[i] = r->array[r->asize];
			if (decref) {
				DEC_SREF(s);
			}
		} else {
			++i;
		}
	}
}

static struct ssm_ref *
mergeset(struct ssm_ref *set, struct ssm_ref * rset, int changeref) {
	if (set == NULL)
		return rset;
	else if (rset == NULL)
		return set;
	int total = set->nuse + rset->nuse;
	if (total * 2 <= set->hsize) {
		shrink_ref(set);
	} else if (total > set->hsize) {
		expand_ref(set, changeref);
	}
	int i;
	for (i=0;i<rset->hsize;i++) {
		TString * s = rset->hash[i];
		if (s) {
			markref(set, s, changeref);
		}
	}
	for (i=0;i<rset->asize;i++) {
		TString * s = rset->array[i];
		markref(set, s, changeref);
	}
	delete_ref(rset);
	remove_duplicate(set, changeref);
	return set;
}

static void
merge_last(struct collect_queue * c) {
	void *key = c->key;
	int hash = (int)((uintptr_t)key % VMHASHSLOTS);
	struct shrmap * s = &SSM;
	struct collect_queue * slot = s->vm[hash];
	if (slot == NULL) {
		s->vm[hash] = c;
		c->next = NULL;
		return;
	}

	if (slot->key == key) {
		// remove head
		s->vm[hash] = slot->next;
	} else {
		for (;;) {
			struct collect_queue * next = slot->next;
			if (next == NULL) {
				// not found, insert head
				c->next = s->vm[hash];
				s->vm[hash] = c;
				return;
			} else if (next->key == key) {
				// remove next
				slot->next = next->next;
				slot = next;
				break;
			}
			slot = next;
		}
	}
	// merge slot (last) into c
	c->strsave = mergeset(slot->strsave, c->strsave, 1);
	c->strfix = mergeset(slot->strfix, c->strfix, 0);
	c->next = s->vm[hash];
	s->vm[hash] = c;
	free(slot);
}

static void
clear_vm(struct collect_queue * c) {
	void *key = c->key;
	int hash = (int)((uintptr_t)key % VMHASHSLOTS);
	struct shrmap * s = &SSM;
	struct collect_queue * slot = s->vm[hash];
	lua_assert(slot == c);
	s->vm[hash] = slot->next;
	delete_cqueue(slot);
}

static int
compar_tstring(const void *a, const void *b) {
	return memcmp(a,b, sizeof(TString *));
}

static void
sortset(struct ssm_ref *set) {
	qsort(set->array, set->asize,sizeof(TString *),compar_tstring);
}

static int
exist(struct ssm_ref *r, TString *s) {
	int slot = lmod(s->hash, r->hsize);
	TString *hs = r->hash[slot];
	if (hs == s)
		return 1;
	int begin = 0, end = r->asize-1;
	while (begin <= end) {
		int mid = (begin + end) / 2;
		TString *t = r->array[mid];
		if (t == s)
			return 1;
		if (memcmp(&s,&t,sizeof(TString *)) > 0)
			begin = mid + 1;
		else
			end = mid - 1;
	}
	return 0;
}

static int
collectref(struct collect_queue * c) {
	int i;
	int total = 0;
	merge_last(c);
	struct ssm_ref *mark = c->strmark;
	struct ssm_ref * save = c->strsave;
	c->strmark = NULL;
	if (mark) {
		struct ssm_ref * fix = c->strfix;
		sortset(mark);
		sortset(fix);

		for (i=0;i<save->hsize;i++) {
			TString * s = save->hash[i];
			if (s) {
				if (!exist(mark, s) && !exist(fix, s)) {
					save->hash[i] = NULL;
					--save->nuse;
					DEC_SREF(s);
					++total;
				}
			}
		}

		for (i=0;i<save->asize;) {
			TString * s = save->array[i];
			if (!exist(mark, s) && !exist(fix, s)) {
				--save->asize;
				--save->nuse;
				save->array[i] = save->array[save->asize];
				DEC_SREF(s);
				++total;
			} else {
				++i;
			}
		}
		delete_ref(mark);
	} else {
		for (i=0;i<save->hsize;i++) {
			TString * s = save->hash[i];
			if (s) {
				DEC_SREF(s);
				++total;
			}
		}
		for (i=0;i<save->asize;i++) {
			TString * s = save->array[i];
			DEC_SREF(s);
			++total;
		}
		clear_vm(c);
	}
	return total;
}

static int
pow2size(struct ssm_ref *r) {
	if (r->nuse <= MINSTRTABSIZE)
		return MINSTRTABSIZE;
	int hsize = r->hsize;
	while (hsize / 2 > r->nuse) {
		hsize /= 2;
	}
	return hsize;
}

void
luaS_collect(global_State *g, int closed) {
	if (closed) {
		delete_ref(g->strmark);
		g->strmark = NULL;
	}
	struct shrmap * s = &SSM;
	struct collect_queue *cqueue = (struct collect_queue *)malloc(sizeof(*cqueue));
	if (cqueue == NULL) {
		/* OOM, give up */
		return;
	}
	cqueue->key = g;
	cqueue->strsave = g->strsave;
	cqueue->strmark = g->strmark;
	cqueue->strfix = g->strfix;
	cqueue->next = NULL;

	g->strfix = NULL;
	if (closed) {
		g->strsave = NULL;
		g->strmark = NULL;
	} else {
		g->strsave = newref(pow2size(g->strsave));
		g->strmark = newref(pow2size(g->strmark));
	}

	spinlock_lock(&s->qlock);
		if (s->head) {
			s->tail->next = cqueue;
			s->tail = cqueue;
		} else {
			s->head = s->tail = cqueue;
		}
	spinlock_unlock(&s->qlock);
}

static unsigned int make_str_seed() {
	size_t buff[4];
	unsigned int h = time(NULL);
	buff[0] = cast(size_t, h);
	buff[1] = cast(size_t, &STRSEED);
	buff[2] = cast(size_t, &make_str_seed);
	buff[3] = cast(size_t, SHRSTR_INITSIZE);
	return luaS_hash((const char*)buff, sizeof(buff), h);
}

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
				TString * next = (TString *)str->next;
				FREE_SREF(str);
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
	if (s->rwslots != osz)
		return 0;
	s->readonly = s->readwrite;
	s->readwrite = newpage;
	s->roslots = s->rwslots;
	s->rwslots = sz;

	return 1;
}

static void
shrstr_rehash(struct shrmap *s, int slotid) {
	struct shrmap_slot *slot = &s->readonly[slotid];
	rwlock_wlock(&slot->lock);
		TString *str = slot->str;
		while (str) {
			TString * next = (TString *)str->next;
			if (ZERO_SREF(str)) {
				FREE_SREF(str);
			} else {
				int newslotid = lmod(str->hash, s->rwslots);
				struct shrmap_slot *newslot = &s->readwrite[newslotid];
				rwlock_wlock(&newslot->lock);
					str->next = (GCObject *)newslot->str;
					newslot->str = str;
				rwlock_wunlock(&newslot->lock);
			}
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
expandssm() {
	struct shrmap * s = &SSM;
	if (s->readonly)
		return;
	int osz = s->rwslots;
	int sz = osz * 2;
	if (sz < osz) {
		// overflow check
		return;
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

static int
sweep_slot(struct shrmap *s, int i) {
	struct shrmap_slot *slot = &s->readwrite[i];
	int n = 0;
	TString *ts;
	rwlock_rlock(&slot->lock);
		ts = slot->str;
		while (ts) {
			if (ZERO_SREF(ts)) {
				n = 1;
				break;
			}
			ts = (TString *)ts->next;
		}
	rwlock_runlock(&slot->lock);
	if (n == 0)
		return 0;

	n = 0;
	rwlock_wlock(&slot->lock);
		TString **ref = &slot->str;
		ts = *ref;
		while (ts) {
			if (ZERO_SREF(ts)) {
				*ref = (TString *)ts->next;
				FREE_SREF(ts);
				ts = *ref;
				++n;
			} else {
				ref = (TString **)&(ts->next);
				ts = *ref;
			}
		}
	rwlock_wunlock(&slot->lock);
	return n;
}

static int
sweepssm() {
	struct shrmap * s = &SSM;
	rwlock_rlock(&s->lock);
		if (s->readonly) {
			rwlock_runlock(&s->lock);
			return 0;
		}
		int sz = s->rwslots;
		int i;
		int n = 0;
		for (i=0;i<sz;i++) {
			n += sweep_slot(s, i);
		}
	rwlock_runlock(&s->lock);
	return n;
}

/* call it in a separate thread */
LUA_API int
luaS_collectssm(struct ssm_collect *info) {
	struct shrmap * s = &SSM;
	if (s->total * 5 / 4 > s->rwslots) {
		expandssm();
	}
	if (s->garbage > s->total / 8) {
		info->sweep = sweepssm();
	} else {
		info->sweep = 0;
	}
	if (s->head) {
		struct collect_queue * cqueue;
		spinlock_lock(&s->qlock);
			cqueue = s->head;
			s->head = cqueue->next;
		spinlock_unlock(&s->qlock);
		if (cqueue) {
			if (info) {
				info->key = cqueue->key;
			}
			int n = collectref(cqueue);
			if (info) {
				info->n = n;
			}
		}
		return 1;
	}
	return 0;
}

LUA_API void
luaS_initssm() {
	struct shrmap * s = &SSM;
	rwlock_init(&s->lock);
	s->rwslots = SHRSTR_INITSIZE;
	s->readwrite = shrstr_newpage(SHRSTR_INITSIZE);
	s->readonly = NULL;
	s->head = NULL;
	s->tail = NULL;
	spinlock_init(&s->qlock);
	STRSEED = make_str_seed();
}

LUA_API void
luaS_exitssm() {
	struct shrmap * s = &SSM;
	rwlock_wlock(&s->lock);
	int sz = s->rwslots;
	shrstr_deletepage(s->readwrite, sz);
	shrstr_deletepage(s->readonly, s->roslots);
	s->readwrite = NULL;
	s->readonly = NULL;
	free_cqueue(s->head);
	s->head = NULL;
	s->tail = NULL;
	int i;
	for (i=0;i<VMHASHSLOTS;i++) {
		free_cqueue(s->vm[i]);
		s->vm[i] = NULL;
	}
}

static TString *
find_string(struct shrmap_slot * slot, unsigned int h, const char *str, lu_byte l) {
	TString *ts = slot->str;
	while (ts) {
		if (ts->hash == h &&
			ts->shrlen == l &&
			memcmp(str, ts+1, l) == 0) {
			ADD_SREF(ts);
			break;
		}
		ts = (TString *)ts->next;
	}
	return ts;
}

static TString *
find_and_collect(struct shrmap_slot * slot, unsigned int h, const char *str, lu_byte l) {
	TString **ref = &slot->str;
	TString *ts = *ref;
	while (ts) {
		if (ts->hash == h &&
			ts->shrlen == l &&
			memcmp(str, ts+1, l) == 0) {
			ADD_SREF(ts);
			break;
		}
		if (ZERO_SREF(ts)) {
			*ref = (TString *)ts->next;
			FREE_SREF(ts);
			ts = *ref;
		} else {
			ref = (TString **)&(ts->next);
			ts = *ref;
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
query_string(unsigned int h, const char *str, lu_byte l) {
	struct shrmap * s = &SSM;
	TString *ts = NULL;
	rwlock_rlock(&s->lock);
		struct shrmap_slot *slot = &s->readwrite[lmod(h, s->rwslots)];
		rwlock_rlock(&slot->lock);
			ts = find_string(slot, h, str, l);
		rwlock_runlock(&slot->lock);
		if (ts == NULL && s->readonly != NULL) {
			int mask = s->roslots - 1;
			slot = &s->readonly[h & mask];
			rwlock_rlock(&slot->lock);
				ts = find_string(slot, h, str, l);
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
	setbits(ts->marked, WHITEBITS);
	gray2black(ts);
	ts->tt = LUA_TSHRSTR;
	ts->hash = h;
	ts->shrlen = l;
	ts->u.ref = 1;
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
			found = find_string(slot, h, str, l);
		rwlock_runlock(&slot->lock);
		if (found)
			return found;
	}
	struct shrmap_slot *slot = &s->readwrite[lmod(h, s->rwslots)];
	rwlock_wlock(&slot->lock);
	if (s->readonly) {
		// Don't collect during expanding
		found = find_string(slot, h, str, l);
	} else {
		found = find_and_collect(slot, h, str, l);
	}
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
			struct shrmap_slot *slot = &s->readwrite[lmod(h, s->rwslots)];
			ts = tmp;
			ts->next = (GCObject *)slot->str;
			slot->str = ts;
			rwlock_wunlock(&slot->lock);
			ATOM_INC(&SSM.total);
		}
	rwlock_runlock(&s->lock);
	return ts;
}

static TString *
internshrstr(lua_State *L, const char *str, size_t l) {
	TString *ts;
	unsigned int h = luaS_hash(str, l, STRSEED);
	ts = query_string(h, str, l);
	if (ts == NULL) {
		ts = add_string(h, str, l);
	}
	markref(G(L)->strsave, ts, 1);
	return ts;
}

struct slotinfo {
	int len;
	int garbage;
	size_t size;
	size_t garbage_size;
};

static void
getslot(struct shrmap_slot *s, struct slotinfo *info) {
	memset(info, 0, sizeof(*info));
	rwlock_rlock(&s->lock);
	TString *ts = s->str;
	while (ts) {
		++info->len;
		size_t sz = sizelstring(ts->shrlen);
		if (ZERO_SREF(ts)) {
			++info->garbage;
			info->garbage_size += sz;
		}
		info->size += sz;
		ts = (TString *)ts->next;
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

LUA_API void
luaS_infossm(struct ssm_info *info) {
	struct slotinfo total;
	struct slotinfo tmp;
	memset(&total, 0, sizeof(total));
	struct shrmap * s = &SSM;
	struct variance v = { 0,0,0 };
	int slots = 0;
	rwlock_rlock(&s->lock);
		int i;
		int sz = s->rwslots;
		for (i=0;i<sz;i++) {
			struct shrmap_slot *slot = &s->readwrite[i];
			getslot(slot, &tmp);
			if (tmp.len > 0) {
				if (tmp.len > total.len) {
					total.len = tmp.len;
				}
				total.size += tmp.size;
				total.garbage_size += tmp.garbage_size;
				total.garbage += tmp.garbage;
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
					// may double counting, but it's only an info
					total.size += tmp.size;
					total.garbage_size += tmp.garbage_size;
					total.garbage += tmp.garbage;
					variance_update(&v, tmp.len);
				}
			}
		}
	rwlock_runlock(&s->lock);
	info->total = SSM.total;
	info->size = total.size;
	info->longest = total.len;
	info->slots = slots;
	info->garbage = total.garbage;
	info->garbage_size = total.garbage_size;
	if (v.count > 1) {
		info->variance = v.m2 / v.count;
	} else {
		info->variance = 0;
	}
}
