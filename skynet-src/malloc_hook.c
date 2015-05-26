#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <stdlib.h>

#include "malloc_hook.h"
#include "skynet.h"

// 当前内存使用的容量
static size_t _used_memory = 0;

// 当前内存块的次数, 也可以理解为分配了多少次的内存
static size_t _memory_block = 0;

// 记录内存使用量的数据结构
typedef struct _mem_data {
	uint32_t handle;	// 对应的 handle
	ssize_t allocated;	// 对应 handle 分配对应内存的容量统计
} mem_data;

#define SLOT_SIZE 0x10000
#define PREFIX_SIZE sizeof(uint32_t)

static mem_data mem_stats[SLOT_SIZE];


#ifndef NOUSE_JEMALLOC

#include "jemalloc.h"

/**
 * 获得 data->allocated 的指针, 注意, 下面的操作都会涉及到多线程, 逻辑上会相对复杂一些.
 * @param handle 索引
 * @return 返回 data->allocated 的指针
 */
static ssize_t*
get_allocated_field(uint32_t handle) {

	// h 最大值到 65535, SLOT_SIZE
	int h = (int)(handle & (SLOT_SIZE - 1));
	mem_data *data = &mem_stats[h];
	uint32_t old_handle = data->handle;
	ssize_t old_alloc = data->allocated;

	// 判断当前这个 handle 是否已经被占用, 或者是否已经分配了内存.
	if(old_handle == 0 || old_alloc <= 0) {

		// 将 handle 赋值给 data->handle, 并且成功之后, 才继续进行下面的处理.
		if(!__sync_bool_compare_and_swap(&data->handle, old_handle, handle)) {
			return 0;
		}

		// data->allocated may less than zero, because it may not count at start.
		// 译文: data->allocated 可能小于 0, 因为它可能在开始的时候没有计数.
		if (old_alloc < 0) {
			// 保证其他线程没有对 data->allocated 做操作的时候, 设置 data->allocated 为 0.
			__sync_bool_compare_and_swap(&data->allocated, old_alloc, 0);
		}
	}

	// 这层判断是为了防止 data->handle 被其他线程修改, 如果又被其他线程做了修改, 那么之前这个线程的操作就是无效的.
	if(data->handle != handle) {
		return 0;
	}

	return &data->allocated;
}

/**
 * 分配内存, 更新统计数据
 * @param handle ID
 * @param __n 分配内存的大小
 */
inline static void 
update_xmalloc_stat_alloc(uint32_t handle, size_t __n) {

	// _used_memory += __n
	__sync_add_and_fetch(&_used_memory, __n);

	// _memory_block += 1
	__sync_add_and_fetch(&_memory_block, 1); 

	// allocated += __n
	ssize_t* allocated = get_allocated_field(handle);
	if(allocated) {
		__sync_add_and_fetch(allocated, __n);
	}
}

/**
 * 释放内存, 更新统计数据
 * @param handle ID
 * @param __n 释放内存的大小
 */
inline static void
update_xmalloc_stat_free(uint32_t handle, size_t __n) {

	// _used_memory -= __n
	__sync_sub_and_fetch(&_used_memory, __n);

	// _memory_block -= 1
	__sync_sub_and_fetch(&_memory_block, 1);

	// allocated -= __n
	ssize_t* allocated = get_allocated_field(handle);
	if(allocated) {
		__sync_sub_and_fetch(allocated, __n);
	}
}

/**
 * 在可用内存 ptr 的末尾(prefix?) 记录 handle 数据, 更新新分配内存数据的记录.
 * @param ptr 新分配内存的起始指针
 * @return ptr
 */
inline static void*
fill_prefix(char* ptr) {
	uint32_t handle = skynet_current_handle();
	size_t size = je_malloc_usable_size(ptr);
	uint32_t *p = (uint32_t *)(ptr + size - sizeof(uint32_t));
	memcpy(p, &handle, sizeof(handle));

	update_xmalloc_stat_alloc(handle, size);
	return ptr;
}

/**
 * 更新删除内存数据的记录.
 * @param ptr 释放内存的起始指针
 * @return ptr
 */
inline static void*
clean_prefix(char* ptr) {
	size_t size = je_malloc_usable_size(ptr);
	uint32_t *p = (uint32_t *)(ptr + size - sizeof(uint32_t));
	uint32_t handle;
	memcpy(&handle, p, sizeof(handle));
	update_xmalloc_stat_free(handle, size);
	return ptr;
}

/**
 * 内存不足的警告
 * @param size 分配的内存大小
 */
static void malloc_oom(size_t size) {
	fprintf(stderr, "xmalloc: Out of memory trying to allocate %zu bytes\n",
		size);
	fflush(stderr);
	abort();
}

/**
 * 打印当前的内存信息
 */
void 
memory_info_dump(void) {
	je_malloc_stats_print(0,0,0);
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

/// malloc 的 skynet 版本
void *
skynet_malloc(size_t size) {
	void* ptr = je_malloc(size + PREFIX_SIZE);
	if(!ptr) malloc_oom(size);
	return fill_prefix(ptr);
}

/// realloc 的 skynet 版本
void *
skynet_realloc(void *ptr, size_t size) {
	if (ptr == NULL) return skynet_malloc(size);

	void* rawptr = clean_prefix(ptr);
	void *newptr = je_realloc(rawptr, size+PREFIX_SIZE);
	if(!newptr) malloc_oom(size);
	return fill_prefix(newptr);
}

/// free 的 skynet 版本
void
skynet_free(void *ptr) {
	if (ptr == NULL) return;
	void* rawptr = clean_prefix(ptr);
	je_free(rawptr);
}

/// calloc 的 skynet 版本
void *
skynet_calloc(size_t nmemb,size_t size) {
	void* ptr = je_calloc(nmemb + ((PREFIX_SIZE+size-1)/size), size );
	if(!ptr) malloc_oom(size);
	return fill_prefix(ptr);
}

#else

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
		mem_data* data = &mem_stats[i];
		if(data->handle != 0 && data->allocated != 0) {
			total += data->allocated;
			skynet_error(NULL, "0x%x -> %zdkb", data->handle, data->allocated >> 10);
		}
	}
	skynet_error(NULL, "+total: %zdkb",total >> 10);
}

/// str 字符串复制
char *
skynet_strdup(const char *str) {
	size_t sz = strlen(str);
	char * ret = skynet_malloc(sz+1);
	memcpy(ret, str, sz+1);
	return ret;
}

/// lalloc 的 skynet 版本
void * 
skynet_lalloc(void *ud, void *ptr, size_t osize, size_t nsize) {
	if (nsize == 0) {
		skynet_free(ptr);
		return NULL;
	} else {
		return skynet_realloc(ptr, nsize);
	}
}
