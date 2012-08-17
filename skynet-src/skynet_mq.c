#include "skynet_mq.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#define DEFAULT_QUEUE_SIZE 64;

struct message_queue {
	uint32_t handle;
	int cap;
	int head;
	int tail;
	int lock;
	int in_global;
	struct skynet_message *queue;
};

struct global_queue {
	int cap;
	int head;
	int tail;
	int lock;
	struct message_queue ** queue;
};

static struct global_queue *Q = NULL;

static inline void
_lock_global_queue() {
	while (__sync_lock_test_and_set(&Q->lock,1)) {}
}

#define LOCK(q) while (__sync_lock_test_and_set(&(q)->lock,1)) {}
#define UNLOCK(q) __sync_lock_release(&(q)->lock);

static void 
skynet_globalmq_push(struct message_queue * queue) {
	struct global_queue *q= Q;
	LOCK(q)

	q->queue[q->tail] = queue;
	if (++ q->tail >= q->cap) {
		q->tail = 0;
	}

	if (q->head == q->tail) {
		struct message_queue **new_queue = malloc(sizeof(struct message_queue *) * q->cap * 2);
		int i;
		for (i=0;i<q->cap;i++) {
			new_queue[i] = q->queue[(q->head + i) % q->cap];
		}
		q->head = 0;
		q->tail = q->cap;
		q->cap *= 2;
		
		free(q->queue);
		q->queue = new_queue;
	}

	UNLOCK(q)
}

struct message_queue * 
skynet_globalmq_pop() {
	struct global_queue *q = Q;
	struct message_queue * ret = NULL;
	LOCK(q)

	if (q->head != q->tail) {
		ret = q->queue[q->head];
		if ( ++ q->head >= q->cap) {
			q->head = 0;
		}
	}

	UNLOCK(q)

	return ret;
}

struct message_queue * 
skynet_mq_create(uint32_t handle) {
	struct message_queue *q = malloc(sizeof(*q));
	q->handle = handle;
	q->cap = DEFAULT_QUEUE_SIZE;
	q->head = 0;
	q->tail = 0;
	q->lock = 0;
	q->in_global = 1;
	q->queue = malloc(sizeof(struct skynet_message) * q->cap);

	return q;
}

void 
skynet_mq_release(struct message_queue *q) {
	free(q->queue);
	free(q);
}

uint32_t 
skynet_mq_handle(struct message_queue *q) {
	return q->handle;
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

void 
skynet_mq_push(struct message_queue *q, struct skynet_message *message) {
	LOCK(q)

	if (message) {
		q->queue[q->tail] = *message;
		if (++ q->tail >= q->cap) {
			q->tail = 0;
		}

		if (q->head == q->tail) {
			struct skynet_message *new_queue = malloc(sizeof(struct skynet_message) * q->cap * 2);
			int i;
			for (i=0;i<q->cap;i++) {
				new_queue[i] = q->queue[(q->head + i) % q->cap];
			}
			q->head = 0;
			q->tail = q->cap;
			q->cap *= 2;
			
			free(q->queue);
			q->queue = new_queue;
		}
	}

	if (q->in_global == 0) {
		skynet_globalmq_push(q);
		q->in_global = 1;
	}
	
	UNLOCK(q)
}

void 
skynet_mq_init(int n) {
	struct global_queue *q = malloc(sizeof(*q));
	memset(q,0,sizeof(*q));
	int cap = 2;
	while (cap < n) {
		cap *=2;
	}
	
	q->cap = cap;
	q->queue = malloc(cap * sizeof(struct skynet_message*));
	Q=q;
}

void 
skynet_mq_force_push(struct message_queue * queue) {
	assert(queue->in_global);
	skynet_globalmq_push(queue);
}
