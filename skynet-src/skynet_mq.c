#include "skynet.h"
#include "skynet_mq.h"
#include "skynet_handle.h"
#include "skynet_multicast.h"

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
	int release;
	int lock_session;
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

static void 
_pushhead(struct message_queue *q, struct skynet_message *message) {
	int head = q->head - 1;
	if (head < 0) {
		head = q->cap - 1;
	}
	if (head == q->tail) {
		expand_queue(q);
		--q->tail;
		head = q->cap - 1;
	}

	q->queue[head] = *message;
	q->head = head;

	// this api use in push a unlock message, so the in_global flags must be 1 , but the q is not exist in global queue.
	assert(q->in_global);
	skynet_globalmq_push(q);
}

void 
skynet_mq_push(struct message_queue *q, struct skynet_message *message) {
	assert(message);
	LOCK(q)
	
	if (q->lock_session !=0 && message->session == q->lock_session) {
		_pushhead(q,message);
		q->lock_session = 0;
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
				q->in_global = 1;
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
	q->lock_session = session;
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
	q->queue = malloc(cap * sizeof(struct message_queue *));
	Q=q;
}

void 
skynet_mq_force_push(struct message_queue * queue) {
	assert(queue->in_global);
	skynet_globalmq_push(queue);
}

void 
skynet_mq_pushglobal(struct message_queue *queue) {
	assert(queue->in_global);
	if (queue->lock_session == 0) {
		skynet_globalmq_push(queue);
	}
}

void 
skynet_mq_mark_release(struct message_queue *q) {
	assert(q->release == 0);
	q->release = 1;
}

static int
_drop_queue(struct message_queue *q) {
	// todo: send message back to message source
	struct skynet_message msg;
	int s = 0;
	while(!skynet_mq_pop(q, &msg)) {
		++s;
		int type = msg.sz >> HANDLE_REMOTE_SHIFT;
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
	
	if (q->release) {
		UNLOCK(q)
		ret = _drop_queue(q);
	} else {
		skynet_mq_force_push(q);
		UNLOCK(q)
	}
	
	return ret;
}
