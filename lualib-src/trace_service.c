#include "trace_service.h"

#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <time.h>
#include <stdint.h>

#define HASH_SIZE 32

#if defined(__APPLE__)
#include <mach/task.h>
#include <mach/mach.h>
#endif

void
current_time(struct timespec *ti) {
#if  !defined(__APPLE__)
	clock_gettime(CLOCK_THREAD_CPUTIME_ID, ti);
#else
	struct task_thread_times_info aTaskInfo;
	mach_msg_type_number_t aTaskInfoCount = TASK_THREAD_TIMES_INFO_COUNT;
	assert(KERN_SUCCESS == task_info(mach_task_self(), TASK_THREAD_TIMES_INFO, (task_info_t )&aTaskInfo, &aTaskInfoCount));
	ti->tv_sec = aTaskInfo.user_time.seconds;
	ti->tv_nsec = aTaskInfo.user_time.microseconds * 1000;
#endif
}

void 
diff_time(struct timespec *ti, uint32_t *sec, uint32_t *nsec) {
	struct timespec end;
	current_time(&end);
	int diffsec = end.tv_sec - ti->tv_sec;
	assert(diffsec>=0);
	int diffnsec = end.tv_nsec - ti->tv_nsec;
	if (diffnsec < 0) {
		--diffsec;
		diffnsec += NANOSEC;
	}
	*nsec += diffnsec;
	if (*nsec > NANOSEC) {
		++*sec;
		*nsec -= NANOSEC;
	}
	*sec += diffsec;
}

struct trace_info {
	int session;
	struct trace_info * prev;
	struct trace_info * next;
	struct timespec ti;
	uint32_t ti_sec;
	uint32_t ti_nsec;
};

struct trace_pool {
	struct trace_info * current;
	struct trace_info * slot[HASH_SIZE];
};

struct trace_pool *
trace_create() {
	struct trace_pool * p = malloc(sizeof(*p));
	memset(p, 0, sizeof(*p));
	return p;
}

static void
_free_slot(struct trace_info *t) {
	while (t) {
		struct trace_info *next = t->next;
		free(t);
		t = next;
	}
}

void
trace_release(struct trace_pool *p) {
	int i;
	for (i=0;i<HASH_SIZE;i++) {
		_free_slot(p->slot[i]);
	}
	free(p->current);
}

struct trace_info *
trace_new(struct trace_pool *p) {
	if (p->current) {
		return NULL;
	}
	struct trace_info *t = malloc(sizeof(*t));
	p->current = t;
	t->session = 0;
	t->prev = NULL;
	t->next = NULL;
	t->ti_sec = 0;
	t->ti_nsec = 0;
	current_time(&t->ti);
	return t;
}

void 
trace_register(struct trace_pool *p, int session) {
	struct trace_info *t = p->current;
	if (t == NULL) {
		return;
	}
	int hash = session % HASH_SIZE;
	assert(t->session == 0 && session > 0);
	t->session = session;
	t->prev = NULL;
	t->next = p->slot[hash];
	if (p->slot[hash]) {
		p->slot[hash]->prev = t;
	}
	p->slot[hash] = t;
}

struct trace_info * 
trace_yield(struct trace_pool *p) {
	struct trace_info *t = p->current;
	if (t == NULL)
		return NULL;

	diff_time(&t->ti,&t->ti_sec,&t->ti_nsec);

	if (t->session == 0) {
		return t;
	} else {
		p->current = NULL;
		return NULL;
	}
}

void 
trace_switch(struct trace_pool *p, int session) {
	assert(p->current == NULL && session > 0);
	struct trace_info *t = p->slot[session % HASH_SIZE];
	struct trace_info *prev = NULL;
	while (t) {
		if (t->session == session) {
			p->current = t;
			t->session = 0;
			if (prev == NULL) {
				p->slot[session % HASH_SIZE] = t->next;
			} else {
				prev->next = t->next;
			}
			if (t->next) {
				t->next->prev = prev;
			}
			current_time(&t->ti);
			return;
		}
		prev = t;
		t=t->next;
	}
}

double 
trace_delete(struct trace_pool *p, struct trace_info *t) {
	assert(p->current == t);
	p->current = NULL;
	if (t) {
		double ti = (double)t->ti_sec + (double)t->ti_nsec / NANOSEC;
		free(t);
		return ti;
	} else {
		return 0;
	}
}
