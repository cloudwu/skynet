#include <string.h>

#include "mem_info.h"

void
meminfo_init(MemInfo *info) {
    memset(info, 0, sizeof(*info));
}

void
atomic_meminfo_init(AtomicMemInfo *info) {
    ATOM_INIT(&info->alloc, 0);
    ATOM_INIT(&info->alloc_count, 0);
    ATOM_INIT(&info->free, 0);
    ATOM_INIT(&info->free_count, 0);
}

void
meminfo_alloc(MemInfo *info, size_t size) {
    info->alloc += size;
    ++info->alloc_count;
}

void
atomic_meminfo_alloc(AtomicMemInfo *info, size_t size) {
    ATOM_FADD(&info->alloc, size);
    ATOM_FADD(&info->alloc_count, 1);
}

void
meminfo_free(MemInfo *info, size_t size) {
    info->free += size;
    ++info->free_count;
}

void
atomic_meminfo_free(AtomicMemInfo *info, size_t size) {
    ATOM_FADD(&info->free, size);
    ATOM_FADD(&info->free_count, 1);
}

void
meminfo_merge(MemInfo *dest, const MemInfo *src) {
    dest->alloc += src->alloc;
    dest->alloc_count += src->alloc_count;
    dest->free += src->free;
    dest->free_count += src->free_count;
}

void
atomic_meminfo_merge(MemInfo *dest, const AtomicMemInfo *src) {
    MemInfo info;
    // 先加载 free 后加载 alloc 避免大小错乱
    info.free_count = ATOM_LOAD(&src->free_count);
    info.free = ATOM_LOAD(&src->free);
    info.alloc_count = ATOM_LOAD(&src->alloc_count);
    info.alloc = ATOM_LOAD(&src->alloc);
    meminfo_merge(dest, &info);
}
