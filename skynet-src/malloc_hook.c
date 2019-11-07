#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <stdlib.h>
#include <lua.h>
#include <stdio.h>

#include "malloc_hook.h"
#include "skynet.h"
#include "atomic.h"

// turn on MEMORY_CHECK can do more memory check, such as double free
// #define MEMORY_CHECK

#define MEMORY_CODECACHE_HANDLE 0xC0DECACE

#define MEMORY_ALLOCTAG 0x20140605
#define MEMORY_FREETAG 0x0badf00d

static size_t _used_memory = 0;
static size_t _memory_block = 0;

struct mem_data {
	uint32_t handle;
	ssize_t allocated;
	uint32_t blockalloc;
	uint32_t blockfree;
};

struct mem_cookie {
	uint32_t tag;
	uint32_t handle;
#ifdef MEMORY_CHECK
	uint32_t dogtag;
#endif
};

#define SLOT_SIZE 0x10000
#define TAG_SLOT_SIZE 0x1000
#define PREFIX_SIZE sizeof(struct mem_cookie)

static struct mem_data mem_stats[SLOT_SIZE];
static struct mem_data tag_stats[TAG_SLOT_SIZE];
static uint32_t tag_handle = MEMORY_CODECACHE_HANDLE;

#ifndef NOUSE_JEMALLOC

#include "jemalloc.h"

// for skynet_lalloc use
#define raw_realloc je_realloc
#define raw_free je_free

static struct mem_data*
get_allocated_field(uint32_t handle) {
	int h = (int)(handle & (SLOT_SIZE - 1));
	struct mem_data *data = &mem_stats[h];
	uint32_t old_handle = data->handle;
	ssize_t old_alloc = data->allocated;
	if(old_handle == 0 || old_alloc <= 0) {
		// data->allocated may less than zero, because it may not count at start.
		if(!ATOM_CAS(&data->handle, old_handle, handle)) {
			return 0;
		}
		if (old_alloc < 0) {
			ATOM_CAS(&data->allocated, old_alloc, 0);
		}
	}
	if(data->handle != handle) {
		return 0;
	}
	return data;
}

static struct mem_data*
get_tag_slot(uint32_t tag) {
	int h = (int)(tag & (TAG_SLOT_SIZE - 1));
	struct mem_data *data = &tag_stats[h];
	uint32_t old_handle = data->handle;
	struct mem_data *start_data = data;
	while(old_handle != tag) {
		if (old_handle == 0) {
			if(ATOM_CAS(&data->handle, old_handle, tag)) {
				break;
			}
		}
		data++;
		if (data >= tag_stats+TAG_SLOT_SIZE) {
			data = tag_stats + 1;
		}
		if (data == start_data)
			return NULL;
		old_handle = data->handle;
	}
	return data;
}

inline static void
update_xmalloc_stat_alloc(uint32_t handle, uint32_t tag, size_t __n) {
	ATOM_ADD(&_used_memory, __n);
	ATOM_INC(&_memory_block);
	struct mem_data* data = get_allocated_field(handle);
	if(data) {
		ATOM_ADD(&data->allocated, __n);
		ATOM_INC(&data->blockalloc);
	}
	if (handle != tag_handle)
		return;
	struct mem_data* tdata = get_tag_slot(tag);
	if(tdata) {
		ATOM_ADD(&tdata->allocated, __n);
		ATOM_INC(&tdata->blockalloc);
	}
}

inline static void
update_xmalloc_stat_free(uint32_t handle, uint32_t tag, size_t __n) {
	ATOM_SUB(&_used_memory, __n);
	ATOM_DEC(&_memory_block);
	struct mem_data* data = get_allocated_field(handle);
	if(data) {
		ATOM_SUB(&data->allocated, __n);
		ATOM_INC(&data->blockfree);
	}
	if (handle != tag_handle)
		return;
	struct mem_data* tdata = get_tag_slot(tag);
	if(tdata) {
		ATOM_SUB(&tdata->allocated, __n);
		ATOM_INC(&tdata->blockfree);
	}
}

inline static void*
fill_prefix(char* ptr, uint32_t tag) {
	uint32_t handle = skynet_current_handle();
	size_t size = je_malloc_usable_size(ptr);
	struct mem_cookie *p = (struct mem_cookie *)(ptr + size - sizeof(struct mem_cookie));
	memcpy(&p->handle, &handle, sizeof(handle));
	memcpy(&p->tag, &tag, sizeof(tag));
#ifdef MEMORY_CHECK
	uint32_t dogtag = MEMORY_ALLOCTAG;
	memcpy(&p->dogtag, &dogtag, sizeof(dogtag));
#endif
	update_xmalloc_stat_alloc(handle, tag, size);
	return ptr;
}

inline static void*
clean_prefix(char* ptr) {
	size_t size = je_malloc_usable_size(ptr);
	struct mem_cookie *p = (struct mem_cookie *)(ptr + size - sizeof(struct mem_cookie));
	uint32_t handle;
	memcpy(&handle, &p->handle, sizeof(handle));
	uint32_t tag;
	memcpy(&tag, &p->tag, sizeof(tag));
#ifdef MEMORY_CHECK
	uint32_t dogtag;
	memcpy(&dogtag, &p->dogtag, sizeof(dogtag));
	if (dogtag == MEMORY_FREETAG) {
		fprintf(stderr, "xmalloc: double free in :%08x\n", handle);
	}
	assert(dogtag == MEMORY_ALLOCTAG);	// memory out of bounds
	dogtag = MEMORY_FREETAG;
	memcpy(&p->dogtag, &dogtag, sizeof(dogtag));
#endif
	update_xmalloc_stat_free(handle, tag, size);
	return ptr;
}

static void malloc_oom(size_t size) {
	fprintf(stderr, "xmalloc: Out of memory trying to allocate %zu bytes\n",
		size);
	fflush(stderr);
	abort();
}

void
memory_info_dump(void) {
	je_malloc_stats_print(0,0,0);
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
skynet_malloc_tag(unsigned tag, size_t size) {
	void* ptr = je_malloc(size + PREFIX_SIZE);
	if(!ptr) malloc_oom(size);
	return fill_prefix(ptr, tag);
}

void *
skynet_malloc_raw(size_t size) {
	void* ptr = je_malloc(size + PREFIX_SIZE);
	if(!ptr) malloc_oom(size);
	return fill_prefix(ptr, 0);
}

void *
skynet_realloc_tag(unsigned tag, void *ptr, size_t size) {
	if (ptr == NULL) return skynet_malloc_tag(tag, size);

	void* rawptr = clean_prefix(ptr);
	void *newptr = je_realloc(rawptr, size+PREFIX_SIZE);
	if(!newptr) malloc_oom(size);
	return fill_prefix(newptr, tag);
}

void *
skynet_realloc_raw(void *ptr, size_t size) {
	if (ptr == NULL) return skynet_malloc(size);

	void* rawptr = clean_prefix(ptr);
	void *newptr = je_realloc(rawptr, size+PREFIX_SIZE);
	if(!newptr) malloc_oom(size);
	return fill_prefix(newptr, 0);
}

void
skynet_free(void *ptr) {
	if (ptr == NULL) return;
	void* rawptr = clean_prefix(ptr);
	je_free(rawptr);
}

void *
skynet_calloc_tag(unsigned tag, size_t nmemb, size_t size) {
	void* ptr = je_calloc(nmemb + ((PREFIX_SIZE+size-1)/size), size );
	if(!ptr) malloc_oom(size);
	return fill_prefix(ptr, tag);
}

void *
skynet_calloc_raw(size_t nmemb, size_t size) {
	void* ptr = je_calloc(nmemb + ((PREFIX_SIZE+size-1)/size), size );
	if(!ptr) malloc_oom(size);
	return fill_prefix(ptr, 0);
}

void *
skynet_memalign(size_t alignment, size_t size) {
	void* ptr = je_memalign(alignment, size + PREFIX_SIZE);
	if(!ptr) malloc_oom(size);
	return fill_prefix(ptr, 0);
}

void *
skynet_aligned_alloc(size_t alignment, size_t size) {
	void* ptr = je_aligned_alloc(alignment, size + (size_t)((PREFIX_SIZE + alignment -1) & ~(alignment-1)));
	if(!ptr) malloc_oom(size);
	return fill_prefix(ptr, 0);
}

int
skynet_posix_memalign(void **memptr, size_t alignment, size_t size) {
	int err = je_posix_memalign(memptr, alignment, size + PREFIX_SIZE);
	if (err) malloc_oom(size);
	fill_prefix(*memptr, 0);
	return err;
}

#else

// for skynet_lalloc use
#define raw_realloc realloc
#define raw_free free

void
memory_info_dump(void) {
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
	return _used_memory;
}

size_t
malloc_memory_block(void) {
	return _memory_block;
}

void
dump_c_mem() {
	int i;
	size_t total = 0;
	skynet_error(NULL, "dump all service mem:");
	for(i=0; i<SLOT_SIZE; i++) {
		struct mem_data* data = &mem_stats[i];
		if(data->handle != 0 && data->allocated != 0) {
			total += data->allocated;
			skynet_error(NULL, ":%08x -> %zdkb %db", data->handle, data->allocated >> 10, (int)(data->allocated % 1024));
		}
	}
	skynet_error(NULL, "+total: %zdkb",total >> 10);
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

void *
codecache_lalloc (void *ud, void *ptr, size_t osize, size_t nsize) {
	if (ptr != NULL && osize > 0) {
		update_xmalloc_stat_free(MEMORY_CODECACHE_HANDLE, (uint32_t)ud,  osize);
	}
	if (nsize > 0) {
		update_xmalloc_stat_alloc(MEMORY_CODECACHE_HANDLE, (uint32_t)ud, nsize);
	}
	return skynet_lalloc(ptr, osize, nsize);
}

int
dump_mem_lua(lua_State *L) {
	int i;
	lua_newtable(L);
	for(i=0; i<SLOT_SIZE; i++) {
		struct mem_data* data = &mem_stats[i];
		if(data->handle != 0 && data->allocated != 0) {
			lua_newtable(L);
			lua_pushinteger(L, data->allocated);
			lua_rawseti(L, -2, 1);
			lua_pushinteger(L, data->blockalloc);
			lua_rawseti(L, -2, 2);
			lua_pushinteger(L, data->blockfree);
			lua_rawseti(L, -2, 3);
			lua_rawseti(L, -2, (lua_Integer)data->handle);
		}
	}
	lua_newtable(L);
	for(i=0; i<TAG_SLOT_SIZE; i++) {
		struct mem_data* data = &tag_stats[i];
		if(data->allocated != 0) {
			lua_newtable(L);
			lua_pushinteger(L, data->allocated);
			lua_rawseti(L, -2, 1);
			lua_pushinteger(L, data->blockalloc);
			lua_rawseti(L, -2, 2);
			lua_pushinteger(L, data->blockfree);
			lua_rawseti(L, -2, 3);
			lua_rawseti(L, -2, (lua_Integer)data->handle);
		}
	}
	lua_pushinteger(L, tag_handle);
	return 3;
}

size_t
malloc_current_memory(void) {
	uint32_t handle = skynet_current_handle();
	int i;
	for(i=0; i<SLOT_SIZE; i++) {
		struct mem_data* data = &mem_stats[i];
		if(data->handle == (uint32_t)handle && data->allocated != 0) {
			return (size_t) data->allocated;
		}
	}
	return 0;
}

void
skynet_debug_memory(const char *info) {
	// for debug use
	uint32_t handle = skynet_current_handle();
	size_t mem = malloc_current_memory();
	fprintf(stderr, "[:%08x] %s %p\n", handle, info, (void *)mem);
}

void
skynet_memory_watch(uint32_t handle) {
	tag_handle = handle;
	memset(tag_stats, 0, sizeof(tag_stats));
}
