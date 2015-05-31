#ifndef SKYNET_MALLOC_HOOK_H
#define SKYNET_MALLOC_HOOK_H

#include <stdlib.h>

// 得到当前已经使用的内存大小
extern size_t malloc_used_memory(void);

// 当前 memory_block 的数量
extern size_t malloc_memory_block(void);

// 输出当前的内存信息
extern void   memory_info_dump(void);

// 类似键值的一个结构, name 是键, newval 是值, 返回 name 当前的值.
// 当 newval 不为 NULL 的时候, name 对应的值将更新为 newval, 并且返回.
// 当 newval 为 NULL 的时候, 读出 name 对应的值
extern size_t mallctl_int64(const char* name, size_t* newval);

// 与上面的 mallctl_opt 类似, 只不过这次操作的是 int 类型
extern int    mallctl_opt(const char* name, int* newval);

// 打印当前所有使用的内存信息
extern void   dump_c_mem(void);

#endif /* SKYNET_MALLOC_HOOK_H */

