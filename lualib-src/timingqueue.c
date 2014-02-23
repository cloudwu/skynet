#include "timingqueue.h"

#include <stdlib.h>
#include <assert.h>
#include <stdio.h>

#define INIT_CAP 8

struct session_time {
	int session;
	double time;
};

struct tqueue {
	int cap;
	int n;
	int head;
	int tail;
	struct session_time * q;
};

struct tqueue *
tqueue_new() {
	struct tqueue * tq = malloc(sizeof(*tq));
	tq->cap = INIT_CAP;
	tq->head = 0;
	tq->tail = 0;
	tq->n = 0;
	tq->q = malloc(sizeof(struct session_time) * tq->cap);
	return tq;
}

void
tqueue_delete(struct tqueue *tq) {
	free(tq->q);
	free(tq);
}

static struct session_time *
expand_queue(struct tqueue *tq) {
	if (tq->n == tq->cap) {
		struct session_time * q = malloc(sizeof(*q) * tq->cap * 2);
		int i;
		for (i=0;i<tq->cap;i++) {
			int index = tq->head + i;
			if (index >= tq->cap) {
				index -= tq->cap;
			}
			q[i] = tq->q[index];
		}
		tq->head = 0;
		tq->tail = tq->cap;
		tq->cap *= 2;
		free(tq->q);
		tq->q = q;
		return &q[tq->tail-1];
	} else {
		int i;
		for (i=0;i<tq->cap-1;i++) {
			int index = tq->head + i;
			if (index >= tq->cap) {
				index -= tq->cap;
			}
			if (tq->q[index].session == 0)
				break;
		}
		int p = i + tq->head;
		if (p >= tq->cap)
			p -= tq->cap;
		for (++i;i<tq->cap-1;i++) {
			int index = tq->head + i;
			if (index >= tq->cap) {
				index -= tq->cap;
			}
			if (tq->q[index].session == 0) {
				continue;
			}
			tq->q[p] = tq->q[index];
			++ p;
			if (p >= tq->cap) {
				p -= tq->cap;
			}
		}
		tq->tail = p + 1;
		if (tq->tail >= tq->cap) {
			tq->tail -= tq->cap;
		}
		return &tq->q[p];
	}
}

static inline struct session_time *
last_one(struct tqueue * tq, struct session_time *current) {
	--current;
	if (current < tq->q) {
		return &tq->q[tq->cap-1];
	}
	return current;
}

void
tqueue_push(struct tqueue *tq, int session, double time) {
	assert(session !=0);
	++ tq->n;
	if (tq->head == tq->tail) {
		// queue is empty;
		tq->head = 0;
		tq->tail = 1;
		tq->q[0].session = session;
		tq->q[0].time = time;
		return;
	}
	struct session_time * st = &tq->q[tq->tail++];
	if (tq->tail >= tq->cap) {
		tq->tail = 0;
	}
	if (tq->tail == tq->head) {
		st = expand_queue(tq);
	}
	st->session = session;
	st->time = time;

	// session must great than last one
	int i;
	for (i=1;i<tq->n;i++) {
		struct session_time *last = last_one(tq, st);
		if (session > last->session)
			return;
		// swap st, last
		struct session_time temp = *last;
		*last = *st;
		*st = temp;

		st = last;
	}
}

double
tqueue_pop(struct tqueue *tq, int session) {
	if (tq->head == tq->tail) {
		return 0;
	}
	if (session == tq->q[tq->head].session) {
		--tq->n;
		double ret = tq->q[tq->head].time;
		do {
			++tq->head;
			if (tq->head == tq->cap) {
				tq->head = 0;
			}
		} while (tq->head != tq->tail && tq->q[tq->head].session == 0);
		return ret;
	}
	// binary search session
	int n = tq->tail - tq->head;
	if (n < 0) {
		n += tq->cap;
	}
	int begin = 1;
	int end = n;
	while (begin < end) {
		int mid = (begin + end)/2;
		int index = mid + tq->head;
		if (index >= tq->cap) {
			index -= tq->cap;
		}
		struct session_time *st = &tq->q[index];
		if (st->session == session) {
			--tq->n;
			st->session = 0;
			return st->time;
		}
		if (session > st->session) {
			begin = mid + 1;
		} else {
			end = mid;
		}
	}
	// not found
	return 0;
}

void
tqueue_dump(struct tqueue *tq) {
	printf("cap = %d, head = %d, tail = %d, n = %d\n",
		tq->cap, tq->head, tq->tail, tq->n);
	int head = tq->head;
	while (head!=tq->tail) {
		struct session_time *st = &tq->q[head];
		if (st->session != 0) {
			printf("%d : %f\n", st->session, st->time);
		}
		++ head;
		if (head >= tq->cap) 
			head -= tq->cap;
	}
}
