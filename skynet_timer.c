 #include "skynet_timer.h"
#include "skynet_mq.h"

#include <time.h>
#include <assert.h>
#include <string.h>
#include <stdlib.h>

typedef void (*timer_execute_func)(void *ud,void *arg);

#define TIME_NEAR_SHIFT 8
#define TIME_NEAR (1 << TIME_NEAR_SHIFT)
#define TIME_LEVEL_SHIFT 6
#define TIME_LEVEL (1 << TIME_LEVEL_SHIFT)
#define TIME_NEAR_MASK (TIME_NEAR-1)
#define TIME_LEVEL_MASK (TIME_LEVEL-1)

struct timer_node {
	struct timer_node *next;
	int expire;
};

struct link_list {
	struct timer_node head;
	struct timer_node *tail;
};

struct timer {
	struct link_list near[TIME_NEAR];
	struct link_list t[4][TIME_LEVEL-1];
	int lock;
	int time;
	uint32_t current;
};

static struct timer * TI = NULL;

static inline struct timer_node *
link_clear(struct link_list *list)
{
	struct timer_node * ret = list->head.next;
	list->head.next = 0;
	list->tail = &(list->head);

	return ret;
}

static inline void
link(struct link_list *list,struct timer_node *node)
{
	list->tail->next = node;
	list->tail = node;
	node->next=0;
}

static void
add_node(struct timer *T,struct timer_node *node)
{
	int time=node->expire;
	int current_time=T->time;
	
	if ((time|TIME_NEAR_MASK)==(current_time|TIME_NEAR_MASK)) {
		link(&T->near[time&TIME_NEAR_MASK],node);
	}
	else {
		int i;
		int mask=TIME_NEAR << TIME_LEVEL_SHIFT;
		for (i=0;i<3;i++) {
			if ((time|(mask-1))==(current_time|(mask-1))) {
				break;
			}
			mask <<= TIME_LEVEL_SHIFT;
		}
		link(&T->t[i][((time>>(TIME_NEAR_SHIFT + i*TIME_LEVEL_SHIFT)) & TIME_LEVEL_MASK)-1],node);	
	}
}

static void
timer_add(struct timer *T,void *arg,size_t sz,int time)
{
	struct timer_node *node = (struct timer_node *)malloc(sizeof(*node)+sz);
	memcpy(node+1,arg,sz);

	while (__sync_lock_test_and_set(&T->lock,1)) {};

		node->expire=time+T->time;
		add_node(T,node);

	__sync_lock_release(&T->lock);
}

static void 
timer_execute(struct timer *T)
{
	while (__sync_lock_test_and_set(&T->lock,1)) {};
	int idx=T->time & TIME_NEAR_MASK;
	struct timer_node *current;
	int mask,i,time;
	
	while (T->near[idx].head.next) {
		current=link_clear(&T->near[idx]);
		
		do {
			struct timer_node *temp=current;
			skynet_mq_push((struct skynet_message *)(temp+1));
			current=current->next;
			free(temp);	
		} while (current);
	}
	
	++T->time;
	
	mask = TIME_NEAR;
	time = T->time >> TIME_NEAR_SHIFT;
	i=0;
	
	while ((T->time & (mask-1))==0) {
		idx=time & TIME_LEVEL_MASK;
		if (idx!=0) {
			--idx;
			current=link_clear(&T->t[i][idx]);
			while (current) {
				struct timer_node *temp=current->next;
				add_node(T,current);
				current=temp;
			}
			break;				
		}
		mask <<= TIME_LEVEL_SHIFT;
		time >>= TIME_LEVEL_SHIFT;
		++i;
	}	
	__sync_lock_release(&T->lock);
}

static struct timer *
timer_create_timer()
{
	struct timer *r=(struct timer *)malloc(sizeof(struct timer));
	memset(r,0,sizeof(*r));

	int i,j;

	for (i=0;i<TIME_NEAR;i++) {
		link_clear(&r->near[i]);
	}

	for (i=0;i<4;i++) {
		for (j=0;j<TIME_LEVEL-1;j++) {
			link_clear(&r->t[i][j]);
		}
	}

	r->lock = 0;
	r->current = 0;

	return r;
}

void 
skynet_timeout(int handle, int time, int session) {
	struct skynet_message message;
	message.source = SKYNET_SYSTEM_TIMER;
	message.destination = handle;
	message.data = NULL;
	message.sz = (size_t) session;
	if (time == 0) {
		skynet_mq_push(&message);
	} else {
		timer_add(TI, &message, sizeof(message), time);
	}
}

static uint32_t
_gettime(void) {
	struct timespec ti;
	clock_gettime(CLOCK_MONOTONIC, &ti);
	uint32_t t = (uint32_t)(ti.tv_sec & 0xffffff) * 100;
	t += ti.tv_nsec / 10000000;

	return t;
}

void
skynet_updatetime(void) {
	uint32_t ct = _gettime();
	if (ct > TI->current) {
		int diff = ct-TI->current;
		TI->current = ct;
		int i;
		for (i=0;i<diff;i++) {
			timer_execute(TI);
		}
	}
}

uint32_t 
skynet_gettime(void) {
	return TI->current;
}

void 
skynet_timer_init(void) {
	TI = timer_create_timer();
	TI->current = _gettime();
}
