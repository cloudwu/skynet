#include <string.h>
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>

#include <lauxlib.h>

#include "skynet.h"
#include "atomic.h"

#include "malloc_hook.h"

// turn on MEMORY_CHECK can do more memory check, such as double free
// #define MEMORY_CHECK

#define MEMORY_ALLOCTAG 0x20140605
#define MEMORY_FREETAG 0x0badf00d

struct mem_data {
    alignas(CACHE_LINE_SIZE)
	ATOM_ULONG     handle;
    AtomicMemInfo  info;
};
_Static_assert(sizeof(struct mem_data) % CACHE_LINE_SIZE == 0, "mem_data must be cache-line aligned");

struct mem_cookie {
	size_t size;
	uint32_t handle;
#ifdef MEMORY_CHECK
	uint32_t dogtag;
#endif
	uint32_t cookie_size;	// should be the last
};

#define SLOT_SIZE 0x10000
#define PREFIX_SIZE sizeof(struct mem_cookie)

static struct mem_data mem_stats[SLOT_SIZE];
_Static_assert(alignof(mem_stats) % CACHE_LINE_SIZE == 0, "mem_stats must be cache-line aligned");

static struct mem_data *
get_mem_stat(uint32_t handle) {
	int h = (int)(handle & (SLOT_SIZE - 1));
	struct mem_data *data = &mem_stats[h];
	return data;
}

#ifndef NOUSE_JEMALLOC

#include "jemalloc.h"

// for skynet_lalloc use
#define raw_realloc je_realloc
#define raw_free je_free

inline static void
update_xmalloc_stat_alloc(uint32_t handle, size_t __n) {
	struct mem_data *data = get_mem_stat(handle);
    // 当两个不同的 handle 被哈希到同一个槽位时, 新的服务会覆盖旧服务的数据
    // 这种情况在实际运行中非常罕见, 因为同时存在的服务数量很难超过 65536
    ATOM_STORE(&data->handle, handle);
	atomic_meminfo_alloc(&data->info, __n);
}

inline static void
update_xmalloc_stat_free(uint32_t handle, size_t __n) {
	struct mem_data *data = get_mem_stat(handle);
	atomic_meminfo_free(&data->info, __n);
}

inline static void*
fill_prefix(char* ptr, size_t sz, uint32_t cookie_size) {
	uint32_t handle = skynet_current_handle();
	struct mem_cookie *p = (struct mem_cookie *)ptr;
	char * ret = ptr + cookie_size;
	sz += cookie_size;
	p->size = sz;
	p->handle = handle;
#ifdef MEMORY_CHECK
	p->dogtag = MEMORY_ALLOCTAG;
#endif
	update_xmalloc_stat_alloc(handle, sz);
	memcpy(ret - sizeof(uint32_t), &cookie_size, sizeof(cookie_size));
	return ret;
}

inline static uint32_t
get_cookie_size(char *ptr) {
	uint32_t cookie_size;
	memcpy(&cookie_size, ptr - sizeof(cookie_size), sizeof(cookie_size));
	return cookie_size;
}

inline static void*
clean_prefix(char* ptr) {
	uint32_t cookie_size = get_cookie_size(ptr);
	struct mem_cookie *p = (struct mem_cookie *)(ptr - cookie_size);
	uint32_t handle = p->handle;
#ifdef MEMORY_CHECK
	uint32_t dogtag = p->dogtag;
	if (dogtag == MEMORY_FREETAG) {
		fprintf(stderr, "xmalloc: double free in :%08x\n", handle);
	}
	assert(dogtag == MEMORY_ALLOCTAG);	// memory out of bounds
	p->dogtag = MEMORY_FREETAG;
#endif
	update_xmalloc_stat_free(handle, p->size);
	return p;
}

static void malloc_oom(size_t size) {
	fprintf(stderr, "xmalloc: Out of memory trying to allocate %zu bytes\n",
		size);
	fflush(stderr);
	abort();
}

void
memory_info_dump(const char* opts) {
	je_malloc_stats_print(0,0, opts);
}

bool
mallctl_bool(const char* name, bool* newval) {
	bool v = 0;
	size_t len = sizeof(v);
	if(newval) {
		je_mallctl(name, &v, &len, newval, sizeof(bool));
	} else {
		je_mallctl(name, &v, &len, NULL, 0);
	}
	return v;
}

int
mallctl_cmd(const char* name) {
	return je_mallctl(name, NULL, NULL, NULL, 0);
}

size_t
mallctl_int64(const char* name, size_t* newval) {
	size_t v = 0;
	size_t len = sizeof(v);
	if(newval) {
		je_mallctl(name, &v, &len, newval, sizeof(size_t));
	} else {
		je_mallctl(name, &v, &len, NULL, 0);
	}
	// skynet_error(NULL, "name: %s, value: %zd\n", name, v);
	return v;
}

int
mallctl_opt(const char* name, int* newval) {
	int v = 0;
	size_t len = sizeof(v);
	if(newval) {
		int ret = je_mallctl(name, &v, &len, newval, sizeof(int));
		if(ret == 0) {
			skynet_error(NULL, "set new value(%d) for (%s) succeed\n", *newval, name);
		} else {
			skynet_error(NULL, "set new value(%d) for (%s) failed: error -> %d\n", *newval, name, ret);
		}
	} else {
		je_mallctl(name, &v, &len, NULL, 0);
	}

	return v;
}

// hook : malloc, realloc, free, calloc

void *
skynet_malloc(size_t size) {
	void* ptr = je_malloc(size + PREFIX_SIZE);
	if(!ptr) malloc_oom(size);
	return fill_prefix(ptr, size, PREFIX_SIZE);
}

void *
skynet_realloc(void *ptr, size_t size) {
	if (ptr == NULL) return skynet_malloc(size);

	uint32_t cookie_size = get_cookie_size(ptr);
	void* rawptr = clean_prefix(ptr);
	void *newptr = je_realloc(rawptr, size+cookie_size);
	if(!newptr) malloc_oom(size);
	return fill_prefix(newptr, size, cookie_size);
}

void
skynet_free(void *ptr) {
	if (ptr == NULL) return;
	void* rawptr = clean_prefix(ptr);
	je_free(rawptr);
}

void *
skynet_calloc(size_t nmemb, size_t size) {
	uint32_t cookie_n = (PREFIX_SIZE+size-1)/size;
	void* ptr = je_calloc(nmemb + cookie_n, size);
	if(!ptr) malloc_oom(nmemb * size);
	return fill_prefix(ptr, nmemb * size, cookie_n * size);
}

static inline uint32_t
alignment_cookie_size(size_t alignment) {
	if (alignment >= PREFIX_SIZE)
		return alignment;
	switch (alignment) {
	case 4 :
		return (PREFIX_SIZE + 3) / 4 * 4;
	case 8 :
		return (PREFIX_SIZE + 7) / 8 * 8;
	case 16 :
		return (PREFIX_SIZE + 15) / 16 * 16;
	}
	return (PREFIX_SIZE + alignment - 1) / alignment * alignment;
}

void *
skynet_memalign(size_t alignment, size_t size) {
	uint32_t cookie_size = alignment_cookie_size(alignment);
	void* ptr = je_memalign(alignment, size + cookie_size);
	if(!ptr) malloc_oom(size);
	return fill_prefix(ptr, size, cookie_size);
}

void *
skynet_aligned_alloc(size_t alignment, size_t size) {
	uint32_t cookie_size = alignment_cookie_size(alignment);
	void* ptr = je_aligned_alloc(alignment, size + cookie_size);
	if(!ptr) malloc_oom(size);
	return fill_prefix(ptr, size, cookie_size);
}

int
skynet_posix_memalign(void **memptr, size_t alignment, size_t size) {
	uint32_t cookie_size = alignment_cookie_size(alignment);
	int err = je_posix_memalign(memptr, alignment, size + cookie_size);
	if (err) malloc_oom(size);
	fill_prefix(*memptr, size, cookie_size);
	return err;
}

#else

// for skynet_lalloc use
#define raw_realloc realloc
#define raw_free free

void
memory_info_dump(const char* opts) {
	skynet_error(NULL, "No jemalloc");
}

size_t
mallctl_int64(const char* name, size_t* newval) {
	skynet_error(NULL, "No jemalloc : mallctl_int64 %s.", name);
	return 0;
}

int
mallctl_opt(const char* name, int* newval) {
	skynet_error(NULL, "No jemalloc : mallctl_opt %s.", name);
	return 0;
}

bool
mallctl_bool(const char* name, bool* newval) {
	skynet_error(NULL, "No jemalloc : mallctl_bool %s.", name);
	return 0;
}

int
mallctl_cmd(const char* name) {
	skynet_error(NULL, "No jemalloc : mallctl_cmd %s.", name);
	return 0;
}

#endif

size_t
malloc_used_memory(void) {
	MemInfo total = {};
	for(int i = 0; i < SLOT_SIZE; i++) {
		struct mem_data* data = &mem_stats[i];
		const uint32_t handle = ATOM_LOAD(&data->handle);
		if (handle != 0) {
			atomic_meminfo_merge(&total, &data->info);
		}
	}
	return total.alloc - total.free;
}

size_t
malloc_memory_block(void) {
	MemInfo total = {};
	for(int i = 0; i < SLOT_SIZE; i++) {
		struct mem_data* data = &mem_stats[i];
		const uint32_t handle = ATOM_LOAD(&data->handle);
		if (handle != 0) {
			atomic_meminfo_merge(&total, &data->info);
		}
	}
	return total.alloc_count - total.free_count;
}

void
dump_c_mem() {
	skynet_error(NULL, "dump all service mem:");
	MemInfo total = {};
	for(int i = 0; i < SLOT_SIZE; i++) {
		struct mem_data* data = &mem_stats[i];
		const uint32_t handle = ATOM_LOAD(&data->handle);
		if (handle != 0) {
			MemInfo info = {};
			atomic_meminfo_merge(&info, &data->info);
			meminfo_merge(&total, &info);
			const size_t using = info.alloc - info.free;
			skynet_error(NULL, ":%08x -> %zukb %zub", handle, using >> 10, using);
		}
	}
	const size_t using = total.alloc - total.free;
	skynet_error(NULL, "+total: %zukb", using >> 10);
}

char *
skynet_strdup(const char *str) {
	size_t sz = strlen(str);
	char * ret = skynet_malloc(sz+1);
	memcpy(ret, str, sz+1);
	return ret;
}

void *
skynet_lalloc(void *ptr, size_t osize, size_t nsize) {
	if (nsize == 0) {
		raw_free(ptr);
		return NULL;
	} else {
		return raw_realloc(ptr, nsize);
	}
}

int
dump_mem_lua(lua_State *L) {
	int i;
	lua_newtable(L);
	for(i=0; i<SLOT_SIZE; i++) {
		struct mem_data* data = &mem_stats[i];
		const uint32_t handle = ATOM_LOAD(&data->handle);
		if (handle != 0) {
			MemInfo info = {};
			atomic_meminfo_merge(&info, &data->info);
			lua_pushinteger(L, info.alloc - info.free);
			lua_rawseti(L, -2, handle);
		}
	}
	return 1;
}

size_t
malloc_current_memory(void) {
	uint32_t handle = skynet_current_handle();
	struct mem_data *data = get_mem_stat(handle);
	if (ATOM_LOAD(&data->handle) != handle) {
		return 0;
	}
	MemInfo info = {};
	atomic_meminfo_merge(&info, &data->info);
	return info.alloc - info.free;
}

void
skynet_debug_memory(const char *info) {
	// for debug use
	uint32_t handle = skynet_current_handle();
	size_t mem = malloc_current_memory();
	fprintf(stderr, "[:%08x] %s %p\n", handle, info, (void *)mem);
}
