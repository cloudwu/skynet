#include "skynet_handle.h"
#include "skynet_server.h"
#include "rwlock.h"

#include <stdlib.h>
#include <assert.h>
#include <string.h>

#define DEFAULT_SLOT_SIZE 4

// skynet 服务编号的管理和分配

// handle_name 服务 name 和 对应 handle id 的 hash表
struct handle_name {
	char * name;
	uint32_t handle;
};

// 存储handle与skynet_context的对应关系，是一个哈希表
// 每个服务skynet_context都对应一个不重复的handle
// 通过handle便可获取skynet_context
// 保存了 handle 和 skynet_context的对应
struct handle_storage {
	struct rwlock lock;				// 读写锁

	uint32_t harbor;				// 服务所属 harbor harbor 用于不同主机间通信
	uint32_t handle_index;			// 初始值为1，表示handle句柄起始值从1开始
	int slot_size;					// hash 表空间大小，初始为DEFAULT_SLOT_SIZE
	struct skynet_context ** slot;	// skynet_context 表空间
	
	int name_cap;	// handle_name容量，初始为2，这里 name_cap 与 slot_size 不一样的原因在于，不是每个 handle 都有name
	int name_count;				 // handle_name数
	struct handle_name *name;	 // handle_name表
};

static struct handle_storage *H = NULL;

// 注册ctx，将 ctx 存到 handle_storage 哈希表中，并得到一个handle
uint32_t
skynet_handle_register(struct skynet_context *ctx) {
	struct handle_storage *s = H;

	rwlock_wlock(&s->lock);
	
	for (;;) {
		int i;
		for (i=0; i<s->slot_size; i++) {
			uint32_t handle = (i+s->handle_index) & HANDLE_MASK;
			int hash = handle & (s->slot_size-1);	// 等价于 handle % s->slot_size
			if (s->slot[hash] == NULL) { // 找到未使用的  slot 将这个 ctx 放入这个 slot 中
				s->slot[hash] = ctx;
				s->handle_index = handle + 1; // 移动 handle_index 方便下次使用

				rwlock_wunlock(&s->lock);

				handle |= s->harbor; // harbor 用于不同主机间的通信 handle高8位用于harbor 低24位用于本机的 所以这里需要 |= 下
				skynet_context_init(ctx, handle); // 设置 ctx->handle = handle; 即这个 ctx 的 handle
				return handle;
			}
		}
		assert((s->slot_size*2 - 1) <= HANDLE_MASK); // 确保 扩大2倍空间后 总共handle即 slot的数量不超过 24位的限制

		// 哈希表扩大2倍
		struct skynet_context ** new_slot = malloc(s->slot_size * 2 * sizeof(struct skynet_context *));
		memset(new_slot, 0, s->slot_size * 2 * sizeof(struct skynet_context *));

		// 将原来的数据拷贝到新的空间
		for (i=0;i<s->slot_size;i++) {
			int hash = skynet_context_handle(s->slot[i]) & (s->slot_size * 2 - 1); // 映射新的 hash 值
			assert(new_slot[hash] == NULL);
			new_slot[hash] = s->slot[i];
		}

		free(s->slot); // free old mem
		s->slot = new_slot;
		s->slot_size *= 2;
	}
}

// 收回handle
void
skynet_handle_retire(uint32_t handle) {
	struct handle_storage *s = H;

	rwlock_wlock(&s->lock);

	uint32_t hash = handle & (s->slot_size-1); // 等价于  handle % s->slot_size
	struct skynet_context * ctx = s->slot[hash];

	if (ctx != NULL && skynet_context_handle(ctx) == handle) {
		skynet_context_release(ctx); // free skynet_ctx
		s->slot[hash] = NULL;		// 置空，哈希表腾出空间

		int i;
		int j=0,
			n=s->name_count;
		for (i=0; i<n; ++i) {
			if (s->name[i].handle == handle) { // 在 name 表中 找到 handle 对应的 name free掉
				free(s->name[i].name);
				continue;
			} else if (i!=j) {	// 说明free了一个name
				s->name[j] = s->name[i];	// 因此需要将后续元素移到前面
			}
			++j;
		}
		s->name_count = j;
	}

	rwlock_wunlock(&s->lock);
}

// 收回所有handle
void 
skynet_handle_retireall() {
	struct handle_storage *s = H;

	for (;;) {
		int n=0;
		int i;
		for (i=0;i<s->slot_size;i++) {
			rwlock_rlock(&s->lock);
			struct skynet_context * ctx = s->slot[i];
			rwlock_runlock(&s->lock);
			if (ctx != NULL) {
				++n;
				skynet_handle_retire(skynet_context_handle(ctx));
			}
		}
		if (n==0)
			return;
	}
}

// 通过handle获取skynet_context, skynet_context的引用计数加1
struct skynet_context * 
skynet_handle_grab(uint32_t handle) {
	struct handle_storage *s = H;
	struct skynet_context * result = NULL;

	rwlock_rlock(&s->lock);

	uint32_t hash = handle & (s->slot_size-1);
	struct skynet_context * ctx = s->slot[hash];
	if (ctx && skynet_context_handle(ctx) == handle) {
		result = ctx;
		skynet_context_grab(result); // 	__sync_add_and_fetch(&ctx->ref,1);	skynet_context引用计数加1
	}

	rwlock_runlock(&s->lock);

	return result;
}

// 根据名称查找handle
uint32_t 
skynet_handle_findname(const char * name) {
	struct handle_storage *s = H;

	rwlock_rlock(&s->lock);

	uint32_t handle = 0;

	int begin = 0;
	int end = s->name_count - 1;
	while (begin<=end) {
		int mid = (begin+end)/2; // 这里用的二分查找 听说会有整形溢出 改成减法可以避免 不清楚具体怎么搞
		struct handle_name *n = &s->name[mid];
		int c = strcmp(n->name, name); // // 一直在头部插入 实际上这样插入后 name 会按长度大小排好序 这样就能使用二分查找了
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
		// before之后的元素后移一个位置
		for (i=s->name_count;i>before;i--) {
			s->name[i] = s->name[i-1];
		}
	}
	s->name[before].name = name;
	s->name[before].handle = handle;
	s->name_count ++;
}

// 插入 name 和 handle
static const char *
_insert_name(struct handle_storage *s, const char * name, uint32_t handle) {
	int begin = 0;
	int end = s->name_count - 1;
	// 二分查找
	while (begin<=end) {
		int mid = (begin+end)/2;
		struct handle_name *n = &s->name[mid];
		int c = strcmp(n->name, name);
		if (c==0) {
			return NULL;	// 名称已存在 这里名称不能重复插入
		}
		if (c<0) {
			begin = mid + 1;
		} else {
			end = mid - 1;
		}
	}
	char * result = strdup(name);

	_insert_name_before(s, result, handle, begin); // 一直在头部插入 实际上这样插入后 name 会按长度大小排好序 这样就能使用二分查找了

	return result;
}

// name与handle绑定
// 给服务注册一个名称的时候会用到该函数
const char * 
skynet_handle_namehandle(uint32_t handle, const char *name) {
	rwlock_wlock(&H->lock);

	const char * ret = _insert_name(H, name, handle);

	rwlock_wunlock(&H->lock);

	return ret;
}

// 初始化一个 handle 就是初始化 handle_storage
void 
skynet_handle_init(int harbor) {
	assert(H==NULL);

	struct handle_storage * s = malloc(sizeof(*H));
	s->slot_size = DEFAULT_SLOT_SIZE;	// 表空间初始为DEFAULT_SLOT_SIZE
	s->slot = malloc(s->slot_size * sizeof(struct skynet_context *)); // 为 skynet_ctx 分配空间
	memset(s->slot, 0, s->slot_size * sizeof(struct skynet_context *));

	rwlock_init(&s->lock);
	// reserve 0 for system
	s->harbor = (uint32_t) (harbor & 0xff) << HANDLE_REMOTE_SHIFT;
	s->handle_index = 1;	// handle句柄从1开始,0保留 0听云大讲原来是打算将0号服务作为控制中心的
	s->name_cap = 2;		// 名字容量初始为2
	s->name_count = 0;
	s->name = malloc(s->name_cap * sizeof(struct handle_name));

	H = s;

	// Don't need to free H
}

