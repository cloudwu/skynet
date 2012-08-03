#include "skynet_mq.h"

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

struct message_queue {
	int cap;
	int head;
	int tail;
	int lock;
	struct skynet_message *queue;
};

static struct message_queue *Q = NULL;

struct message_queue * 
skynet_mq_create(int cap) {
	struct message_queue *q = malloc(sizeof(*q));
	q->cap = cap;
	q->head = 0;
	q->tail = 0;
	q->lock = 0;
	q->queue = malloc(sizeof(struct skynet_message) * cap);

	return q;
}

void 
skynet_mq_release(struct message_queue *q) {
	free(q->queue);
	free(q);
}

static inline void
_lock_queue(struct message_queue *q) {
	while (__sync_lock_test_and_set(&q->lock,1)) {}
}

static inline void
_unlock_queue(struct message_queue *q) {
	__sync_lock_release(&q->lock);
}

uint32_t
skynet_mq_leave(struct message_queue *q, struct skynet_message *message) {
	uint32_t ret = 0;
	_lock_queue(q);

	if (q->head != q->tail) {
		*message = q->queue[q->head];
		ret = message->destination;
		if ( ++ q->head >= q->cap) {
			q->head = 0;
		}
	}
	
	_unlock_queue(q);

	return ret;
}

void 
skynet_mq_enter(struct message_queue *q, struct skynet_message *message) {
	_lock_queue(q);

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
	
	_unlock_queue(q);
}

uint32_t 
skynet_mq_pop(struct skynet_message *message) {
	return skynet_mq_leave(Q,message);
}

void 
skynet_mq_push(struct skynet_message *message) {
	skynet_mq_enter(Q,message);
}

void 
skynet_mq_init(int cap) {
	Q = skynet_mq_create(cap);
}
