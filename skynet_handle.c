#include "skynet_handle.h"
#include "skynet_server.h"
#include "rwlock.h"

#include <stdlib.h>
#include <assert.h>
#include <string.h>

#define DEFAULT_SLOT_SIZE 4

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
};

static struct handle_storage *H = NULL;

uint32_t
skynet_handle_register(struct skynet_context *ctx) {
	struct handle_storage *s = H;

	rwlock_wlock(&s->lock);
	
	for (;;) {
		int i;
		for (i=0;i<s->slot_size;i++) {
			int hash = (i+s->handle_index) & (s->slot_size-1);
			if (s->slot[hash] == NULL) {
				s->slot[hash] = ctx;
				uint32_t handle = s->handle_index + i ;
				skynet_context_init(ctx, handle);

				rwlock_wunlock(&s->lock);

				s->handle_index = handle + 1;

				return (handle & HANDLE_MASK) | s->harbor;
			}
		}
		struct skynet_context ** new_slot = malloc(s->slot_size * 2 * sizeof(struct skynet_context *));
		memset(new_slot, 0, s->slot_size * 2 * sizeof(struct skynet_context *));
		for (i=0;i<s->slot_size;i++) {
			int hash = skynet_context_handle(s->slot[i]) & (s->slot_size * 2 - 1);
			assert(new_slot[hash] == NULL);
			new_slot[hash] = s->slot[i];
		}
		free(s->slot);
		s->slot = new_slot;
		s->slot_size *= 2;
	}
}

void
skynet_handle_retire(uint32_t handle) {
	struct handle_storage *s = H;

	rwlock_wlock(&s->lock);

	uint32_t hash = handle & (s->slot_size-1);
	struct skynet_context * ctx = s->slot[hash];
	if (skynet_context_handle(ctx) == handle) {
		skynet_context_release(ctx);
		s->slot[hash] = NULL;

		int i;
		int j=0;
		for (i=0;i<s->name_count;i++,j++) {
			if (s->name[i].handle == handle) {
				free(s->name[i].name);
				++j;
				--s->name_count;
			} else {
				if (i!=j) {
					s->name[i] = s->name[j];
				}
			}
		}
	}

	rwlock_wunlock(&s->lock);
}

struct skynet_context * 
skynet_handle_grab(uint32_t handle) {
	struct handle_storage *s = H;
	struct skynet_context * result = NULL;

	rwlock_rlock(&s->lock);

	uint32_t hash = handle & (s->slot_size-1);
	struct skynet_context * ctx = s->slot[hash];
	if (skynet_context_handle(ctx) == handle) {
		result = ctx;
		skynet_context_grab(result);
	}

	rwlock_runlock(&s->lock);

	return result;
}

uint32_t 
skynet_handle_findname(const char * name) {
	struct handle_storage *s = H;

	rwlock_rlock(&s->lock);

	uint32_t handle = 0;

	int begin = 0;
	int end = s->name_count - 1;
	while (begin<=end) {
		int mid = (begin+end)/2;
		struct handle_name *n = &s->name[mid];
		int c = strcmp(n->name, name);
		if (c==0) {
			handle = n->handle;
			handle |= s->harbor;
			break;
		}
		if (c<0) {
			begin = mid + 1;
		} else {
			end = mid - 1;
		}
	}

	rwlock_runlock(&s->lock);

	return handle;
}

static void
_insert_name_before(struct handle_storage *s, char *name, uint32_t handle, int before) {
	if (s->name_count >= s->name_cap) {
		s->name_cap *= 2;
		struct handle_name * n = malloc(s->name_cap * sizeof(struct handle_name));
		int i;
		for (i=0;i<before;i++) {
			n[i] = s->name[i];
		}
		for (i=before;i<s->name_count;i++) {
			n[i+1] = s->name[i];
		}
		free(s->name);
		s->name = n;
	} else {
		int i;
		for (i=s->name_count;i>=before;i--) {
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
	char * result = strdup(name);

	_insert_name_before(s, result, handle, begin);

	return result;
}

const char * 
skynet_handle_namehandle(uint32_t handle, const char *name) {
	rwlock_wlock(&H->lock);

	const char * ret = _insert_name(H, name, handle);

	rwlock_wunlock(&H->lock);

	return ret;
}

void 
skynet_handle_init(int harbor) {
	assert(H==NULL);
	struct handle_storage * s = malloc(sizeof(*H));
	s->slot_size = DEFAULT_SLOT_SIZE;
	s->slot = malloc(s->slot_size * sizeof(struct skynet_context *));
	memset(s->slot, 0, s->slot_size * sizeof(struct handle_slot *));

	rwlock_init(&s->lock);
	// reserve 0 for system
	s->harbor = (uint32_t) (harbor & 0xff) << HANDLE_REMOTE_SHIFT;
	s->handle_index = 1;
	s->name_cap = 2;
	s->name_count = 0;
	s->name = malloc(s->name_cap * sizeof(struct handle_name));

	H = s;

	// Don't need to free H
}

