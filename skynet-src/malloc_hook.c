#include <stdio.h>
#include <string.h>
#include <assert.h>

#include "malloc_hook.h"

#include "jemalloc.h"

#include "skynet.h"

static size_t _used_memory = 0;
static size_t _memory_block = 0;
typedef struct _mem_data {
    uint32_t handle;
    size_t   allocated;
} mem_data;

#define SLOT_SIZE 0xffff
#define PREFIX_SIZE sizeof(uint32_t)

static mem_data mem_stats[SLOT_SIZE];

static void _init() {
    memset(mem_stats, 0, sizeof(mem_stats));
}

static size_t*
get_allocated_field(handle) {
    int h = (handle & SLOT_SIZE) - 1;
    mem_data *data = &mem_stats[h];
    if(data->handle == 0 || data->allocated == 0) {
        __sync_synchronize();
        if(!__sync_bool_compare_and_swap(&data->handle, 0, handle)) {
            return 0;
        }
    }
    if(data->handle != handle) {
        return 0;
    }
    return &data->allocated;
}

inline static void 
update_xmalloc_stat_alloc(uint32_t handle, size_t __n) {
    __sync_add_and_fetch(&_used_memory, __n);
    __sync_add_and_fetch(&_memory_block, 1); 
    size_t* allocated = get_allocated_field(handle);
    if(allocated) {
        __sync_add_and_fetch(allocated, __n);
    }
}

inline static void
update_xmalloc_stat_free(uint32_t handle, size_t __n) {
    __sync_sub_and_fetch(&_used_memory, __n);
    __sync_sub_and_fetch(&_memory_block, 1);
    size_t* allocated = get_allocated_field(handle);
    if(allocated) {
        __sync_sub_and_fetch(allocated, __n);
    }
}

inline static void*
fill_prefix(char* ptr) {
    uint32_t handle = skynet_current_handle();
    size_t size = je_malloc_usable_size(ptr);
    uint32_t *p = (uint32_t *)(ptr + size - sizeof(uint32_t));
    memcpy(p, &handle, sizeof(handle));

	update_xmalloc_stat_alloc(handle, size);
    return ptr;
}

inline static void*
clean_prefix(char* ptr) {
    uint32_t* rawptr = (uint32_t*)ptr - 1;
    size_t size = je_malloc_usable_size(rawptr);
    uint32_t *p = (uint32_t *)(ptr + size - sizeof(uint32_t));
    uint32_t handle;
    memcpy(&handle, p, sizeof(handle));
    update_xmalloc_stat_free(handle, size);
    return ptr;
}

static void malloc_oom(size_t size) {
    fprintf(stderr, "xmalloc: Out of memory trying to allocate %zu bytes\n",
        size);
    fflush(stderr);
    abort();
}

// hook : malloc, realloc, memalign, free, calloc

void *
malloc(size_t size) {
    void* ptr = je_malloc(size + PREFIX_SIZE);
    if(!ptr) malloc_oom(size);
    return fill_prefix(ptr);
}

void *
realloc(void *ptr, size_t size) {
    if (ptr == NULL) return malloc(size);

    void* rawptr = clean_prefix(ptr);
    void *newptr = je_realloc(rawptr, size+PREFIX_SIZE);
    if(!newptr) malloc_oom(size);
    return fill_prefix(newptr);
}

#ifdef JEMALLOC_OVERRIDE_MEMALIGN

void *
memalign(size_t alignment, size_t size) {
    void *ptr = je_memalign(alignment, size+PREFIX_SIZE);
    if(!ptr) malloc_oom(size);
    return fill_prefix(ptr);
}

#endif

void
free(void *ptr) {
    if (ptr == NULL) return;
    void* rawptr = clean_prefix(ptr);
    je_free(rawptr);
}

void *
calloc(size_t nmemb,size_t size) {
    void* ptr = je_calloc(nmemb + ((PREFIX_SIZE+size-1)/size), size );
    if(!ptr) malloc_oom(size);
    return fill_prefix(ptr);
}

size_t
malloc_used_memory(void) {
    return _used_memory;
}

size_t
malloc_memory_block(void) {
    return _memory_block;
}

void memory_info_dump(void) {
    je_malloc_stats_print(0,0,0);
}

size_t mallctl_int64(const char* name, size_t* newval) {
    size_t v = 0;
    size_t len = sizeof(v);
    if(newval) {
        je_mallctl(name, &v, &len, newval, sizeof(size_t));
    } else {
        je_mallctl(name, &v, &len, NULL, 0);
    }
    // printf("name: %s, value: %zd\n", name, v);
    return v;
}

int mallctl_opt(const char* name, int* newval) {
    int    v   = 0;
    size_t len = sizeof(v);
    if(newval) {
        int ret = je_mallctl(name, &v, &len, newval, sizeof(int));
        if(ret == 0) {
            printf("set new value(%d) for (%s) succeed\n", *newval, name);
        } else {
            printf("set new value(%d) for (%s) failed: error -> %d\n", *newval, name, ret);
        }
    } else {
        je_mallctl(name, &v, &len, NULL, 0);
    }

    return v;
}

void
dump_c_mem() {
    int i;
    size_t total = 0;
    printf("dump all service mem:\n");
    for(i=0; i<SLOT_SIZE; i++) {
        mem_data* data = &mem_stats[i];
        if(data->handle != 0 && data->allocated != 0) {
            total += data->allocated;
            printf("0x%x -> %zdkb\n", data->handle, data->allocated >> 10);
        }
    }
    printf("+total: %zdkb\n",total >> 10);
}

/*
 * init 
 */
void __attribute__ ((constructor)) malloc_hook_init(void){
    _init();
}

