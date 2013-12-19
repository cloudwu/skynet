#include "luaalloc.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>

#define ALLOCCHUNK (16 * 1024 + sizeof(struct memchunk))

struct freenode {
	struct freenode * next;
};

struct memchunk {
	struct memchunk * next;
};

struct skynet_lalloc {
	// small memory list : 8,16,32,64,128,256,512
	struct freenode * freelist[7];
	struct memchunk * chunklist;
	char * ptr;
	char * end;
};

static void
new_chunk(struct skynet_lalloc *lalloc) {
	struct memchunk * mc = malloc(ALLOCCHUNK);
	mc->next = lalloc->chunklist;
	lalloc->ptr = (char *)(mc+1);
	lalloc->end = ((char *)mc) + ALLOCCHUNK;
	lalloc->chunklist = mc;
}

inline static int
size_index(size_t sz) {
	if (sz > 32) {
		if (sz > 128) {
			if (sz > 256)
				return 6;
			else
				return 5;
		} else {
			if (sz > 64)
				return 4;
			else
				return 3;
		}
	} else {
		if (sz > 16)
			return 2;
		else if (sz > 8)
			return 1;
		else
			return 0;
	}
}

#define REALSIZE(idx) (1<<((idx)+3))

static void *
new_small_block(struct skynet_lalloc *lalloc, int idx) {
	struct freenode * fn = lalloc->freelist[idx];
	if (fn) {
		lalloc->freelist[idx] = fn->next;
		return fn;
	} else {
		int rsz = REALSIZE(idx);
		if ((lalloc->end - lalloc->ptr) < rsz) {
			new_chunk(lalloc);
		}
		void * ret = lalloc->ptr;
		lalloc->ptr += rsz;
		return ret;
	}
}

static void
delete_small_block(struct skynet_lalloc *lalloc, void * ptr, int idx) {
	struct freenode * fn = lalloc->freelist[idx];
	struct freenode * node = (struct freenode *)ptr;
	node->next = fn;
	lalloc->freelist[idx] = node;
}

static void *
extend_small_block(struct skynet_lalloc *lalloc, void * ptr, size_t osize, size_t nsize) {
	int oidx = size_index(osize);
	int nidx = size_index(nsize);
	if (oidx == nidx) {
		return ptr;
	}
	void * ret = new_small_block(lalloc,nidx);
	memcpy(ret, ptr, osize < nsize ? osize : nsize);
	delete_small_block(lalloc, ptr, oidx);
	return ret;
}

void * 
skynet_lua_alloc(void *ud, void *ptr, size_t osize, size_t nsize) {
	struct skynet_lalloc *lalloc = ud;
	if (ptr == NULL) {
		if (nsize > 512) {
			return malloc(nsize);
		}
		int idx = size_index(nsize);
		return new_small_block(lalloc, idx);
	} else if (nsize == 0) {
		if (osize > 512) {
			free(ptr);
			return NULL;
		}
		int idx = size_index(osize);
		delete_small_block(lalloc, ptr, idx);
		return NULL;
	} else {
		if (osize > 512) {
			if (nsize > 512) {
				return realloc(ptr, nsize);
			} else {
				int idx = size_index(nsize);
				void * ret = new_small_block(lalloc, idx);
				memcpy(ret, ptr, nsize);
				free(ptr);
				return ret;
			}
		}
		if (nsize > 512) {
			void * buffer = malloc(nsize);
			memcpy(buffer, ptr, osize);
			int idx = size_index(osize);
			delete_small_block(lalloc, ptr, idx);
			return buffer;
		} else {
			return extend_small_block(lalloc, ptr, osize, nsize);
		}
	}
}

struct skynet_lalloc * 
skynet_lalloc_new(size_t prealloc) {
	assert(prealloc > sizeof(struct skynet_lalloc));
	struct skynet_lalloc * lalloc = malloc(prealloc);
	int i;
	for (i=0;i<sizeof(lalloc->freelist)/sizeof(lalloc->freelist[0]);i++) {
		lalloc->freelist[i] = NULL;
	}
	lalloc->chunklist = NULL;
	lalloc->ptr = (char *)(lalloc+1);
	lalloc->end = ((char *)lalloc) + prealloc;
	return lalloc;	
}

void 
skynet_lalloc_delete(struct skynet_lalloc *lalloc) {
	struct memchunk * mc = lalloc->chunklist;
	while(mc) {
		struct memchunk * tmp = mc;
		mc = mc->next;
		free(tmp);
	}
	free(lalloc);
}
