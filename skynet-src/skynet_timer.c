#include "skynet.h"

#include "skynet_timer.h"
#include "skynet_mq.h"
#include "skynet_server.h"
#include "skynet_handle.h"
#include "spinlock.h"

#include <time.h>
#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>

#if defined(__APPLE__)
#include <sys/time.h>
#endif

typedef void (*timer_execute_func)(void *ud,void *arg);

#define TIME_NEAR_SHIFT 8
#define TIME_NEAR (1 << TIME_NEAR_SHIFT)     // 1 * 2^8 = 256
#define TIME_LEVEL_SHIFT 6
#define TIME_LEVEL (1 << TIME_LEVEL_SHIFT)   // 1 * 2^6 = 64
#define TIME_NEAR_MASK (TIME_NEAR-1)         // 0xff
#define TIME_LEVEL_MASK (TIME_LEVEL-1)       // 0x

struct timer_event {
	uint32_t handle;                 // 来自不同service的handle
	int session;                     // 来自不同的session
};

struct timer_node {
	struct timer_node *next;         // 单链表
	uint32_t expire;                 // 倒计的时间点，与服务器启动有关
};

struct link_list {                 
	struct timer_node head;         // 所以这个头并不是一个节点，没有数据，只是一个头，用来找到真正有数据需要加入的
	struct timer_node *tail;
};

struct timer {
	struct link_list near[TIME_NEAR];     // 存储最近的256的时刻
	struct link_list t[4][TIME_LEVEL];    // 
	struct spinlock lock;
	uint32_t time;                        // 此变量是用来真正计算走的时间TI（0.01s)
	uint32_t starttime;                   // 此变量存的是s，刚好是000s
	uint64_t current;                     // 此变量存的多少TI，多少TI
	uint64_t current_point;               // 用来计算过了多少TI的
};

static struct timer * TI = NULL;

// 这个函数并不是删除数据，而是重置一个问题
static inline struct timer_node *
link_clear(struct link_list *list) {                   // 中间timer_node怎么删除掉的
	struct timer_node * ret = list->head.next;
	list->head.next = 0;
	list->tail = &(list->head);

	return ret;
}

static inline void
link(struct link_list *list,struct timer_node *node) { // 添加一个timer_node
	list->tail->next = node;
	list->tail = node;
	node->next=0;
}

static void
add_node(struct timer *T,struct timer_node *node) {
	uint32_t time=node->expire;
	uint32_t current_time=T->time;
	
	// 怎么加入这个节点很复杂
	if ((time|TIME_NEAR_MASK)==(current_time|TIME_NEAR_MASK)) {  // 如果在255以内
		link(&T->near[time&TIME_NEAR_MASK],node);
	} else {
		int i;
		uint32_t mask=TIME_NEAR << TIME_LEVEL_SHIFT;
		for (i=0;i<3;i++) {
			if ((time|(mask-1))==(current_time|(mask-1))) {     // 怎么要这么设计
				break;
			}
			mask <<= TIME_LEVEL_SHIFT;
		}

		link(&T->t[i][((time>>(TIME_NEAR_SHIFT + i*TIME_LEVEL_SHIFT)) & TIME_LEVEL_MASK)],node);	
	}
}

static void
timer_add(struct timer *T,void *arg,size_t sz,int time) {
	struct timer_node *node = (struct timer_node *)skynet_malloc(sizeof(*node)+sz);
	memcpy(node+1,arg,sz);

	SPIN_LOCK(T);

		node->expire=time+T->time;   // expire就是那个倒计时，那么updatetime是怎么更新这个T->time的
		add_node(T,node);

	SPIN_UNLOCK(T);
}

static void
move_list(struct timer *T, int level, int idx) {
	struct timer_node *current = link_clear(&T->t[level][idx]);
	while (current) {
		struct timer_node *temp=current->next;
		add_node(T,current);
		current=temp;
	}
}

static void
timer_shift(struct timer *T) {
	int mask = TIME_NEAR;
	uint32_t ct = ++T->time;                   // 这个值是用来干嘛的
	if (ct == 0) {
		move_list(T, 3, 0);
	} else {
		uint32_t time = ct >> TIME_NEAR_SHIFT;
		int i=0;

		while ((ct & (mask-1))==0) {
			int idx=time & TIME_LEVEL_MASK;
			if (idx!=0) {
				move_list(T, i, idx);
				break;				
			}
			mask <<= TIME_LEVEL_SHIFT;
			time >>= TIME_LEVEL_SHIFT;
			++i;
		}
	}
}

/*
 * @breif用来分发所有skynet_node
*/
static inline void
dispatch_list(struct timer_node *current) {
	do {
		struct timer_event * event = (struct timer_event *)(current+1);
		struct skynet_message message;
		message.source = 0;
		message.session = event->session;
		message.data = NULL;
		message.sz = (size_t)PTYPE_RESPONSE << MESSAGE_TYPE_SHIFT;

		skynet_context_push(event->handle, &message);
		
		struct timer_node * temp = current;
		current=current->next;
		skynet_free(temp);	                       // 这才是释放内存
	} while (current);
}

static inline void
timer_execute(struct timer *T) {
	int idx = T->time & TIME_NEAR_MASK;       // 255
	
	while (T->near[idx].head.next) {
		struct timer_node *current = link_clear(&T->near[idx]);   // 把head所有timer_node取出来，全部执行
		SPIN_UNLOCK(T);
		// dispatch_list don't need lock T
		dispatch_list(current);
		SPIN_LOCK(T);
	}
}

static void 
timer_update(struct timer *T) {
	SPIN_LOCK(T);

	// 这里真没有看懂
	// try to dispatch timeout 0 (rare condition)
	timer_execute(T);                         

	// shift time first, and then dispatch timer message
	timer_shift(T);            // T->time == 0

	timer_execute(T);

	SPIN_UNLOCK(T);
}

// 创建一个timer
static struct timer *
timer_create_timer() {
	struct timer *r=(struct timer *)skynet_malloc(sizeof(struct timer));
	memset(r,0,sizeof(*r));

	int i,j;

	for (i=0;i<TIME_NEAR;i++) {               // 为什么还要分配256个链表
		link_clear(&r->near[i]);
	}

	for (i=0;i<4;i++) {
		for (j=0;j<TIME_LEVEL;j++) {          // 64个链表又有什么用
			link_clear(&r->t[i][j]);
		}
	}

	SPIN_INIT(r)

	r->current = 0;

	return r;
}

/*
 @breif 准备要加入一个倒计时的那个skynet_context对应的handle
 @param time 倒计时  多少TI = 0.01s
 @param session 生成的会话id
*/
int
skynet_timeout(uint32_t handle, int time, int session) {
	if (time <= 0) {
		// 如果<=0
		struct skynet_message message;
		message.source = 0;
		message.session = session;
		message.data = NULL;
		message.sz = (size_t)PTYPE_RESPONSE << MESSAGE_TYPE_SHIFT;  // sz = 0,高8bit用来存储消息类型

		if (skynet_context_push(handle, &message)) {                // 加入到消息队列，这个倒计时已经结束
			return -1;
		}
	} else {
		struct timer_event event;                                   // 加入timer里面
		event.handle = handle;
		event.session = session;
		timer_add(TI, &event, sizeof(event), time);
	}

	return session;
}

// centisecond: 1/100 second
static void
systime(uint32_t *sec, uint32_t *cs) {
#if !defined(__APPLE__)
	struct timespec ti;
	clock_gettime(CLOCK_REALTIME, &ti);
	*sec = (uint32_t)ti.tv_sec;
	*cs = (uint32_t)(ti.tv_nsec / 10000000);
#else
	struct timeval tv;
	gettimeofday(&tv, NULL);
	*sec = tv.tv_sec;
	*cs = tv.tv_usec / 10000;
#endif
}

static uint64_t
gettime() {
	uint64_t t;
#if !defined(__APPLE__)
	struct timespec ti;
	clock_gettime(CLOCK_MONOTONIC, &ti);
	t = (uint64_t)ti.tv_sec * 100;
	t += ti.tv_nsec / 10000000;
#else
	struct timeval tv;
	gettimeofday(&tv, NULL);
	t = (uint64_t)tv.tv_sec * 100;
	t += tv.tv_usec / 10000;
#endif
	return t;
}

void
skynet_updatetime(void) {
	uint64_t cp = gettime();
	if(cp < TI->current_point) {
		skynet_error(NULL, "time diff error: change from %lld to %lld", cp, TI->current_point);
		TI->current_point = cp;
	} else if (cp != TI->current_point) {
		uint32_t diff = (uint32_t)(cp - TI->current_point);  // 过了多少TI
		TI->current_point = cp;                              // 当前cp
		TI->current += diff;                                 // 添加多少TI
		int i;
		for (i=0;i<diff;i++) {
			timer_update(TI);
		}
	}
}

uint32_t
skynet_starttime(void) {
	return TI->starttime;
}

uint64_t 
skynet_now(void) {
	return TI->current;
}

void 
skynet_timer_init(void) {
	TI = timer_create_timer();
	uint32_t current = 0;
	systime(&TI->starttime, &current);             // 如果你看了这个systime还没有明白为什么这个函数会这么写
	TI->current = current;
	TI->current_point = gettime();                 // Ti 用
}

