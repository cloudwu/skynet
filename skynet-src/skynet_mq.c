#include "skynet.h"
#include "skynet_mq.h"
#include "skynet_handle.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdbool.h>

#define DEFAULT_QUEUE_SIZE 64
#define MAX_GLOBAL_MQ 0x10000

// 0 means mq is not in global mq.
// 1 means mq is in global mq , or the message is dispatching.

#define MQ_IN_GLOBAL 1

struct message_queue {
	uint32_t handle;
	int cap;
	int head;
	int tail;
	int lock;
	int release;
	int in_global;
	struct skynet_message *queue;
	struct message_queue *next;
};

struct global_queue {
	uint32_t head;
	uint32_t tail;
	struct message_queue ** queue;
	// We use a separated flag array to ensure the mq is pushed.
	// See the comments below.
	struct message_queue *list;
};

static struct global_queue *Q = NULL;

#define LOCK(q) while (__sync_lock_test_and_set(&(q)->lock,1)) {}
#define UNLOCK(q) __sync_lock_release(&(q)->lock);

#define GP(p) ((p) % MAX_GLOBAL_MQ)

void 
skynet_globalmq_push(struct message_queue * queue) {
	struct global_queue *q= Q;

	uint32_t tail = GP(__sync_fetch_and_add(&q->tail,1));

	// only one thread can set the slot (change q->queue[tail] from NULL to queue)
	if (!__sync_bool_compare_and_swap(&q->queue[tail], NULL, queue)) {
		// The queue may full seldom, save queue in list
		assert(queue->next == NULL);
		struct message_queue * last;
		do {
			last = q->list;
			queue->next = last;
		} while(!__sync_bool_compare_and_swap(&q->list, last, queue));

		return;
	}
}

struct message_queue * 
skynet_globalmq_pop() {
	struct global_queue *q = Q;
	uint32_t head =  q->head;

	if (head == q->tail) {
		// The queue is empty.
		return NULL;
	}

	uint32_t head_ptr = GP(head);

	struct message_queue * list = q->list;
	if (list) {
		// If q->list is not empty, try to load it back to the queue
		struct message_queue *newhead = list->next;
		if (__sync_bool_compare_and_swap(&q->list, list, newhead)) {
			// try load list only once, if success , push it back to the queue.
			list->next = NULL;
			skynet_globalmq_push(list);
		}
	}

	struct message_queue * mq = q->queue[head_ptr];
	if (mq == NULL) {
		// globalmq push not complete
		return NULL;
	}
	if (!__sync_bool_compare_and_swap(&q->head, head, head+1)) {
		return NULL;
	}
	// only one thread can get the slot (change q->queue[head_ptr] to NULL)
	if (!__sync_bool_compare_and_swap(&q->queue[head_ptr], mq, NULL)) {
		return NULL;
	}

	return mq;
}

struct message_queue * 
skynet_mq_create(uint32_t handle) {
	struct message_queue *q = skynet_malloc(sizeof(*q));
	q->handle = handle;
	q->cap = DEFAULT_QUEUE_SIZE;
	q->head = 0;
	q->tail = 0;
	q->lock = 0;
	// When the queue is create (always between service create and service init) ,
	// set in_global flag to avoid push it to global queue .
	// If the service init success, skynet_context_new will call skynet_mq_force_push to push it to global queue.
	q->in_global = MQ_IN_GLOBAL;
	q->release = 0;
	q->queue = skynet_malloc(sizeof(struct skynet_message) * q->cap);
	q->next = NULL;

	return q;
}

static void 
_release(struct message_queue *q) {
	assert(q->next == NULL);
	skynet_free(q->queue);
	skynet_free(q);
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
		return tail - head;
	}
	return tail + cap - head;
}

int
skynet_mq_pop(struct message_queue *q, struct skynet_message *message) {
	int ret = 1;
	LOCK(q)

	if (q->head != q->tail) {
		*message = q->queue[q->head];
		ret = 0;
		if ( ++ q->head >= q->cap) {
			q->head = 0;
		}
	}

	if (ret) {
		q->in_global = 0;
	}
	
	UNLOCK(q)

	return ret;
}

static void
expand_queue(struct message_queue *q) {
	struct skynet_message *new_queue = skynet_malloc(sizeof(struct skynet_message) * q->cap * 2);
	int i;
	for (i=0;i<q->cap;i++) {
		new_queue[i] = q->queue[(q->head + i) % q->cap];
	}
	q->head = 0;
	q->tail = q->cap;
	q->cap *= 2;
	
	skynet_free(q->queue);
	q->queue = new_queue;
}

void 
skynet_mq_push(struct message_queue *q, struct skynet_message *message) {
	assert(message);
	LOCK(q)

	q->queue[q->tail] = *message;
	if (++ q->tail >= q->cap) {
		q->tail = 0;
	}

	if (q->head == q->tail) {
		expand_queue(q);
	}

	if (q->in_global == 0) {
		q->in_global = MQ_IN_GLOBAL;
		skynet_globalmq_push(q);
	}
	
	UNLOCK(q)
}

void 
skynet_mq_init() {
	struct global_queue *q = skynet_malloc(sizeof(*q));
	memset(q,0,sizeof(*q));
	q->queue = skynet_malloc(MAX_GLOBAL_MQ * sizeof(struct message_queue *));
	memset(q->queue, 0, sizeof(struct message_queue *) * MAX_GLOBAL_MQ);
	Q=q;
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

static void
_drop_queue(struct message_queue *q, message_drop drop_func, void *ud) {
	struct skynet_message msg;
	while(!skynet_mq_pop(q, &msg)) {
		drop_func(&msg, ud);
	}
	_release(q);
}

void 
skynet_mq_release(struct message_queue *q, message_drop drop_func, void *ud) {
	LOCK(q)
	
	if (q->release) {
		UNLOCK(q)
		_drop_queue(q, drop_func, ud);
	} else {
		skynet_globalmq_push(q);
		UNLOCK(q)
	}
}
