#include "skynet.h"
#include "skynet_mq.h"
#include "skynet_handle.h"
#include "skynet_multicast.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdbool.h>

// skynet使用了二级队列  从全局的 globe_mq 中取 mq 来处理

#define DEFAULT_QUEUE_SIZE 64       // 默认队列的大小我 64
#define MAX_GLOBAL_MQ 0x10000		// 64K,单机服务上限是64K，因而global mq数量最大值也是64k
									// 服务的id空间是2^24即16M

//http://blog.codingnow.com/2012/08/skynet_bug.html

// 0 means mq is not in global mq.
// 1 means mq is in global mq , or the message is dispatching.
// 2 means message is dispatching with locked session set.
// 3 means mq is not in global mq, and locked session has been set.

#define MQ_IN_GLOBAL 1
#define MQ_DISPATCHING 2
#define MQ_LOCKED 3

// 消息队列(循环队列)，容量不固定，按需增长
// 消息队列 mq 的结构
struct message_queue {
	uint32_t handle;		// 所属服务handle
	int cap;				// 容量
	int head;				// 队头
	int tail;				// 队尾
	int lock;				// 用于实现自旋锁 加锁将这个值设置为1 释放锁将这个值设置为0
	int release;			// 消息队列释放标记，当要释放一个服务的时候 清理标记
							// 不能立即释放该服务对应的消息队列(有可能工作线程还在操作mq)，就需要设置一个标记 标记是否可以释放

	int lock_session;		// 被某个session对应的消息锁定
	int in_global;
	struct skynet_message *queue;	// 消息队列
};

// 全局队列(循环队列，无锁队列)，容量固定64K 二级队列的实现
// 保存了 所有的消息 就是从这个队列中取消息出来做处理
struct global_queue {
	uint32_t head;
	uint32_t tail;
	struct message_queue ** queue;	// 消息队列列表，预留MAX_GLOBAL_MQ(64K)个空间
	bool * flag;	// 与queue对应，预留MAX_GLOBAL_MQ(64K)个空间，用于标识相应位置是否有消息队列
					// 当前实现的无锁队列，需要用到该标记 标记这个位置 tail已经用过了 已经完全将这个消息队列 压入全局的消息队里中
};

static struct global_queue *Q = NULL; // 全局的消息队列  Q

// 利用__sync_lock_test_and_set实现的自旋锁
#define LOCK(q) while (__sync_lock_test_and_set(&(q)->lock,1)) {}	//将q->lock设置为1，并返回修改前的值
#define UNLOCK(q) __sync_lock_release(&(q)->lock);		// 将q->lock置为0

#define GP(p) ((p) % MAX_GLOBAL_MQ) // 得到在队列中的位置

/*
	http://blog.codingnow.com/2012/10/bug_and_lockfree_queue.html
	为了保证在进队列操作的时序。我们在原子递增队列尾指针后，还需要额外一个标记位指示数据已经被完整写入队列，
	这样才能让出队列线程取得正确的数据。
*/
static void 
skynet_globalmq_push(struct message_queue * queue) {
	struct global_queue *q= Q;

	uint32_t tail = GP(__sync_fetch_and_add(&q->tail,1));
	q->queue[tail] = queue; // 将这个消息放入全局队列
	__sync_synchronize();
	q->flag[tail] = true; // 标记这个位置 tail已经用过了 已经完全将这个消息队列 压入全局的消息队里中
}

/*
	一开始，我让 读队列线程 忙等写队列线程 完成写入标记。
	我原本觉得写队列线程递增队列尾指针和写入完成标记间只有一两条机器指令，所以忙等是完全没有问题的。
	但是我错了，再极端情况下（队列中数据很少，并发线程非常多），也会造成问题。
	后来的解决方法是，修改了出队列 api 的语义。
	原来的语义是：
		当队列为空的时候，返回一个 NULL ，否则就一定从队列头部取出一个数据来。
	修改后的语义是：
		返回 NULL 表示取队列失败，这个失败可能是因为队列空，也可能是遇到了竞争。

	我们这个队列仅在一处使用，在使用环境上看，修改这个 api 语义是完全成立的，
	修改后完全解决了我前面的问题，并极大的简化了并发队列处理的代码。
*/
struct message_queue * 
skynet_globalmq_pop() {
	struct global_queue *q = Q;
	uint32_t head =  q->head;
	uint32_t head_ptr = GP(head);

	// 队列为空
	if (head_ptr == GP(q->tail)) {
		return NULL;
	}

	// head所在位置没有mq
	if(!q->flag[head_ptr]) {
		return NULL;
	}

	__sync_synchronize();	// 同步指令 保证前面的指令执行完毕，才会执行后面的指令

	struct message_queue * mq = q->queue[head_ptr];

	// CAS原子性操作 如果q->head == head，则q->head=head+1; 移动头部
	if (!__sync_bool_compare_and_swap(&q->head, head, head+1)) {
		return NULL;
	}

	q->flag[head_ptr] = false; // 消息已经被取走

	return mq;
}

// 创建消息队列，初始容量 DEFAULT_QUEUE_SIZE 64个
struct message_queue * 
skynet_mq_create(uint32_t handle) {
	struct message_queue *q = malloc(sizeof(*q));
	q->handle = handle;
	q->cap = DEFAULT_QUEUE_SIZE;
	q->head = 0;
	q->tail = 0;
	q->lock = 0; // 不锁
	q->in_global = MQ_IN_GLOBAL;
	q->release = 0;
	q->lock_session = 0;
	q->queue = malloc(sizeof(struct skynet_message) * q->cap);

	return q;
}

static void 
_release(struct message_queue *q) {
	free(q->queue);
	free(q);
}

uint32_t 
skynet_mq_handle(struct message_queue *q) {
	return q->handle;
}

int
skynet_mq_length(struct message_queue *q) {
	int head, tail,cap;

	LOCK(q)
	head = q->head;
	tail = q->tail;
	cap = q->cap;
	UNLOCK(q)
	
	if (head <= tail) {
		return tail - head; // 正常 没有循环回来
	}
	return tail + cap - head; // 循环回来了
}

int
skynet_mq_pop(struct message_queue *q, struct skynet_message *message) {
	int ret = 1;
	LOCK(q)

	// 消息队列不为空
	if (q->head != q->tail) {
		*message = q->queue[q->head];
		ret = 0;
		if ( ++ q->head >= q->cap) {
			q->head = 0;
		}
	}

	// 没有消息弹出，消息队列为空，则不再将消息队列压入全局队列 消息队列为空的就是就不再压入 globe_mq 中
	if (ret) {
		q->in_global = 0;
	}
	
	UNLOCK(q)

	return ret;
}

// 扩大mq 2倍的大小扩大
static void
expand_queue(struct message_queue *q) {
	struct skynet_message *new_queue = malloc(sizeof(struct skynet_message) * q->cap * 2);
	int i;
	// 将原队列消息搬到新队列
	for (i=0;i<q->cap;i++) {
		new_queue[i] = q->queue[(q->head + i) % q->cap];
	}
	q->head = 0;
	q->tail = q->cap;
	q->cap *= 2; // 2倍的大小扩大
	
	free(q->queue); // 释放原来的空间
	q->queue = new_queue;
}

static void
_unlock(struct message_queue *q) {
	// this api use in push a unlock message, so the in_global flags must not be 0 , 
	// but the q is not exist in global queue.
	if (q->in_global == MQ_LOCKED) {
		skynet_globalmq_push(q);
		q->in_global = MQ_IN_GLOBAL;
	} else {
		assert(q->in_global == MQ_DISPATCHING);
	}
	q->lock_session = 0;
}

static void 
_pushhead(struct message_queue *q, struct skynet_message *message) {
	int head = q->head - 1;
	if (head < 0) {
		head = q->cap - 1;
	}
	// 队列已满 扩大队列 容量2倍
	if (head == q->tail) {
		expand_queue(q);
		--q->tail;
		head = q->cap - 1;
	}

	q->queue[head] = *message;
	q->head = head;

	_unlock(q);
}

void 
skynet_mq_push(struct message_queue *q, struct skynet_message *message) {
	assert(message);
	LOCK(q)
	
	if (q->lock_session !=0 && message->session == q->lock_session) {
		_pushhead(q,message);
	} else {
		q->queue[q->tail] = *message;
		if (++ q->tail >= q->cap) {
			q->tail = 0;
		}

		if (q->head == q->tail) {
			expand_queue(q);
		}

		if (q->lock_session == 0) {
			if (q->in_global == 0) {
				q->in_global = MQ_IN_GLOBAL;
				skynet_globalmq_push(q);
			}
		}
	}
	
	UNLOCK(q)
}

void
skynet_mq_lock(struct message_queue *q, int session) {
	LOCK(q)
	assert(q->lock_session == 0);
	assert(q->in_global == MQ_IN_GLOBAL);
	q->in_global = MQ_DISPATCHING;
	q->lock_session = session;
	UNLOCK(q)
}

void
skynet_mq_unlock(struct message_queue *q) {
	LOCK(q)
	_unlock(q);
	UNLOCK(q)
}

// 初始化全局消息队列，容量固定64K
// 单机服务最大值64K,因而全局消息队列容量固定64K,方便全局消息队列实现为无锁队列
void 
skynet_mq_init() {
	struct global_queue *q = malloc(sizeof(*q));
	memset(q,0,sizeof(*q));
	q->queue = malloc(MAX_GLOBAL_MQ * sizeof(struct message_queue *)); // 64的消息队列
	q->flag = malloc(MAX_GLOBAL_MQ * sizeof(bool)); // 标志这个位置是否用了
	memset(q->flag, 0, sizeof(bool) * MAX_GLOBAL_MQ);
	Q=q;
}

void 
skynet_mq_force_push(struct message_queue * queue) {
	assert(queue->in_global);
	skynet_globalmq_push(queue);
}

// 将一个消息队列插入到全局队列
void 
skynet_mq_pushglobal(struct message_queue *queue) {
	LOCK(queue)
	assert(queue->in_global);
	if (queue->in_global == MQ_DISPATCHING) {
		// lock message queue just now.
		queue->in_global = MQ_LOCKED;
	}
	if (queue->lock_session == 0) {
		skynet_globalmq_push(queue);
		queue->in_global = MQ_IN_GLOBAL;
	}
	UNLOCK(queue)
}

void 
skynet_mq_mark_release(struct message_queue *q) {
	LOCK(q)
	assert(q->release == 0);
	q->release = 1;
	if (q->in_global != MQ_IN_GLOBAL) {
		skynet_globalmq_push(q);
	}
	UNLOCK(q)
}

// 删除消息队列
static int
_drop_queue(struct message_queue *q) {
	// todo: send message back to message source
	struct skynet_message msg;
	int s = 0;
	while(!skynet_mq_pop(q, &msg)) {
		++s;
		int type = msg.sz >> HANDLE_REMOTE_SHIFT; // 右移 24 位得到高 8 位
		if (type == PTYPE_MULTICAST) {
			assert((msg.sz & HANDLE_MASK) == 0);
			skynet_multicast_dispatch((struct skynet_multicast_message *)msg.data, NULL, NULL);
		} else {
			free(msg.data);
		}
	}
	_release(q);
	return s;
}

int 
skynet_mq_release(struct message_queue *q) {
	int ret = 0;
	LOCK(q)
	
	if (q->release) {	// 有释放标记，则删除消息队列q
		UNLOCK(q)
		ret = _drop_queue(q);
	} else {			// 没有，则重新压入全局队列
		skynet_mq_force_push(q);
		UNLOCK(q)
	}
	
	return ret;
}
