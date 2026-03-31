#include "skynet.h"

#include "skynet_handle.h"
#include "skynet_imp.h"
#include "skynet_server.h"
#include "rwlock.h"
#include "spinlock.h"

#include <stdlib.h>
#include <assert.h>
#include <string.h>

#define HANDLE_CACHE_LINE 64

struct handle_reader_slot {
	ATOM_INT active;
	char _pad[HANDLE_CACHE_LINE - sizeof(ATOM_INT)];
};

static _Thread_local int TLS_SLOT_IDX = -1;

#define DEFAULT_SLOT_SIZE 4
#define MAX_SLOT_SIZE 0x40000000

struct handle_name {
	char * name;
	uint32_t handle;
};

struct handle_storage {
	struct rwlock lock;

	uint32_t harbor;
	uint32_t handle_index;
	int slot_size;
	struct skynet_context ** slot;

	int name_cap;
	int name_count;
	struct handle_name *name;

	// distributed reader slots
	ATOM_INT thread_idx;
	int rslot_count;
	struct handle_reader_slot *rslots;
};

static struct handle_storage *H = NULL;

static inline void
handle_rlock(struct handle_storage *s) {
	if (TLS_SLOT_IDX >= 0 && TLS_SLOT_IDX < s->rslot_count) {
		for (;;) {
			ATOM_STORE(&s->rslots[TLS_SLOT_IDX].active, 1);
			if (!ATOM_LOAD(&s->lock.write)) {
				break;
			}
			// Writer present — back off to avoid deadlock
			ATOM_STORE(&s->rslots[TLS_SLOT_IDX].active, 0);
			while (ATOM_LOAD(&s->lock.write)) { atomic_pause_(); }
		}
	} else {
		rwlock_rlock(&s->lock);
	}
}

static inline void
handle_runlock(struct handle_storage *s) {
	if (TLS_SLOT_IDX >= 0 && TLS_SLOT_IDX < s->rslot_count) {
		ATOM_STORE(&s->rslots[TLS_SLOT_IDX].active, 0);
	} else {
		rwlock_runlock(&s->lock);
	}
}

static inline void
handle_wlock(struct handle_storage *s) {
	rwlock_wlock(&s->lock);
	// Additionally wait for all slot readers
	for (int i = 0; i < s->rslot_count; i++) {
		while (ATOM_LOAD(&s->rslots[i].active)) { atomic_pause_(); }
	}
}

static inline void
handle_wunlock(struct handle_storage *s) {
	rwlock_wunlock(&s->lock);
}

uint32_t
skynet_handle_register(struct skynet_context *ctx) {
	struct handle_storage *s = H;

	handle_wlock(s);

	for (;;) {
		int i;
		uint32_t handle = s->handle_index;
		for (i=0;i<s->slot_size;i++,handle++) {
			if (handle > HANDLE_MASK) {
				// 0 is reserved
				handle = 1;
			}
			int hash = handle & (s->slot_size-1);
			if (s->slot[hash] == NULL) {
				s->slot[hash] = ctx;
				s->handle_index = handle + 1;

				handle_wunlock(s);

				handle |= s->harbor;
				return handle;
			}
		}
		assert((s->slot_size*2 - 1) <= HANDLE_MASK);
		struct skynet_context ** new_slot = skynet_malloc(s->slot_size * 2 * sizeof(struct skynet_context *));
		memset(new_slot, 0, s->slot_size * 2 * sizeof(struct skynet_context *));
		for (i=0;i<s->slot_size;i++) {
			if (s->slot[i]) {
				int hash = skynet_context_handle(s->slot[i]) & (s->slot_size * 2 - 1);
				assert(new_slot[hash] == NULL);
				new_slot[hash] = s->slot[i];
			}
		}
		skynet_free(s->slot);
		s->slot = new_slot;
		s->slot_size *= 2;
	}
}

int
skynet_handle_retire(uint32_t handle) {
	int ret = 0;
	struct handle_storage *s = H;

	handle_wlock(s);

	uint32_t hash = handle & (s->slot_size-1);
	struct skynet_context * ctx = s->slot[hash];

	if (ctx != NULL && skynet_context_handle(ctx) == handle) {
		s->slot[hash] = NULL;
		ret = 1;
		int i;
		int j=0, n=s->name_count;
		for (i=0; i<n; ++i) {
			if (s->name[i].handle == handle) {
				skynet_free(s->name[i].name);
				continue;
			} else if (i!=j) {
				s->name[j] = s->name[i];
			}
			++j;
		}
		s->name_count = j;
	} else {
		ctx = NULL;
	}

	handle_wunlock(s);

	if (ctx) {
		// release ctx may call skynet_handle_* , so wunlock first.
		skynet_context_release(ctx);
	}

	return ret;
}

void
skynet_handle_retireall() {
	struct handle_storage *s = H;
	for (;;) {
		int n=0;
		int i;
		for (i=0;i<s->slot_size;i++) {
			handle_rlock(s);
			struct skynet_context * ctx = s->slot[i];
			uint32_t handle = 0;
			if (ctx) {
				handle = skynet_context_handle(ctx);
				++n;
			}
			handle_runlock(s);
			if (handle != 0) {
				skynet_handle_retire(handle);
			}
		}
		if (n==0)
			return;
	}
}

struct skynet_context *
skynet_handle_grab(uint32_t handle) {
	struct handle_storage *s = H;
	struct skynet_context * result = NULL;

	handle_rlock(s);

	uint32_t hash = handle & (s->slot_size-1);
	struct skynet_context * ctx = s->slot[hash];
	if (ctx && skynet_context_handle(ctx) == handle) {
		result = ctx;
		skynet_context_grab(result);
	}

	handle_runlock(s);

	return result;
}

uint32_t
skynet_handle_findname(const char * name) {
	struct handle_storage *s = H;

	handle_rlock(s);

	uint32_t handle = 0;

	int begin = 0;
	int end = s->name_count - 1;
	while (begin<=end) {
		int mid = (begin+end)/2;
		struct handle_name *n = &s->name[mid];
		int c = strcmp(n->name, name);
		if (c==0) {
			handle = n->handle;
			break;
		}
		if (c<0) {
			begin = mid + 1;
		} else {
			end = mid - 1;
		}
	}

	handle_runlock(s);

	return handle;
}

static void
_insert_name_before(struct handle_storage *s, char *name, uint32_t handle, int before) {
	if (s->name_count >= s->name_cap) {
		s->name_cap *= 2;
		assert(s->name_cap <= MAX_SLOT_SIZE);
		struct handle_name * n = skynet_malloc(s->name_cap * sizeof(struct handle_name));
		int i;
		for (i=0;i<before;i++) {
			n[i] = s->name[i];
		}
		for (i=before;i<s->name_count;i++) {
			n[i+1] = s->name[i];
		}
		skynet_free(s->name);
		s->name = n;
	} else {
		int i;
		for (i=s->name_count;i>before;i--) {
			s->name[i] = s->name[i-1];
		}
	}
	s->name[before].name = name;
	s->name[before].handle = handle;
	s->name_count ++;
}

static const char *
_insert_name(struct handle_storage *s, const char * name, uint32_t handle) {
	int begin = 0;
	int end = s->name_count - 1;
	while (begin<=end) {
		int mid = (begin+end)/2;
		struct handle_name *n = &s->name[mid];
		int c = strcmp(n->name, name);
		if (c==0) {
			return NULL;
		}
		if (c<0) {
			begin = mid + 1;
		} else {
			end = mid - 1;
		}
	}
	char * result = skynet_strdup(name);

	_insert_name_before(s, result, handle, begin);

	return result;
}

const char *
skynet_handle_namehandle(uint32_t handle, const char *name) {
	handle_wlock(H);

	const char * ret = _insert_name(H, name, handle);

	handle_wunlock(H);

	return ret;
}

void
skynet_handle_register_thread(void) {
	int idx = ATOM_FINC(&H->thread_idx);
	if (idx < H->rslot_count) {
		TLS_SLOT_IDX = idx;
	}
}

void
skynet_handle_init(int harbor, int thread) {
	assert(H==NULL);
	struct handle_storage * s = skynet_malloc(sizeof(*H));
	s->slot_size = DEFAULT_SLOT_SIZE;
	s->slot = skynet_malloc(s->slot_size * sizeof(struct skynet_context *));
	memset(s->slot, 0, s->slot_size * sizeof(struct skynet_context *));

	rwlock_init(&s->lock);

	// Distributed reader slots: workers + monitor + timer + socket
	s->rslot_count = thread + 3;
	size_t rslot_sz = (size_t)s->rslot_count * sizeof(struct handle_reader_slot);
	s->rslots = (struct handle_reader_slot *)skynet_malloc(rslot_sz);
	memset(s->rslots, 0, rslot_sz);
	ATOM_INIT(&s->thread_idx, 0);

	// reserve 0 for system
	s->harbor = (uint32_t) (harbor & 0xff) << HANDLE_REMOTE_SHIFT;
	s->handle_index = 1;
	s->name_cap = 2;
	s->name_count = 0;
	s->name = skynet_malloc(s->name_cap * sizeof(struct handle_name));

	H = s;

	// Don't need to free H
}
