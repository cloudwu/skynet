#include "skynet.h"
#include "skynet_multicast.h"
#include "skynet_server.h"
#include "skynet_handle.h"

#include <stdlib.h>
#include <string.h>

struct skynet_multicast_message {
	int ref;
	const void * msg;
	size_t sz;
	uint32_t source;
};

struct skynet_multicast_message * 
skynet_multicast_create(const void * msg, size_t sz, uint32_t source) {
	struct skynet_multicast_message * mc = malloc(sizeof(*mc));
	mc->ref = 0;
	mc->msg = msg;
	mc->sz = sz;
	mc->source = source;
	return mc;
}

void 
skynet_multicast_copy(struct skynet_multicast_message *mc, int copy) {
	__sync_fetch_and_add(&mc->ref, copy);
}

void 
skynet_multicast_dispatch(struct skynet_multicast_message * msg, void * ud, skynet_multicast_func func) {
	if (func) {
		func(ud, msg->source, msg->msg, msg->sz);
	}
	int ref = __sync_sub_and_fetch(&msg->ref, 1);
	if (ref == 0) {
		free((void *)msg->msg);
		free(msg);
	}
}

struct array {
	int cap;
	int number;
	uint32_t *data;
};

struct pair {
	uint32_t handle;
	struct skynet_context * ctx;
};

struct skynet_multicast_group {
	struct array enter_queue;
	struct array leave_queue;
	int cap;
	int number;
	struct pair * data;
};

struct skynet_multicast_group * 
skynet_multicast_newgroup() {
	struct skynet_multicast_group * g = malloc(sizeof(*g));
	memset(g,0,sizeof(*g));
	return g;
}

void 
skynet_multicast_deletegroup(struct skynet_multicast_group * g) {
	int i;
	for (i=0;i<g->number;i++) {
		if (g->data[i].ctx) {
			skynet_context_release(g->data[i].ctx);
		}
	}
	free(g->data);
	free(g->enter_queue.data);
	free(g->leave_queue.data);
	free(g);
}

static void
push_array(struct array * a, uint32_t v) {
	if (a->number >= a->cap) {
		a->cap *= 2;
		if (a->cap == 0) {
			a->cap = 4;
		}
		a->data = realloc(a->data, a->cap * sizeof(uint32_t));
	}
	a->data[a->number++] = v;
}

void 
skynet_multicast_entergroup(struct skynet_multicast_group * group, uint32_t handle) {
	push_array(&group->enter_queue, handle);
}

void 
skynet_multicast_leavegroup(struct skynet_multicast_group * group, uint32_t handle) {
	push_array(&group->leave_queue, handle);
}


static int
compar_uint(const void *a, const void *b) {
	const uint32_t * aa = a;
	const uint32_t * bb = b;
	return (int)(*aa - *bb);
}

static void
combine_queue(struct skynet_context * from, struct skynet_multicast_group * group) {
	qsort(group->enter_queue.data, group->enter_queue.number, sizeof(uint32_t), compar_uint);
	qsort(group->leave_queue.data, group->leave_queue.number, sizeof(uint32_t), compar_uint);
	int i;
	int enter = group->enter_queue.number;
	uint32_t last = 0;

	int new_size = group->number + enter;
	if (new_size > group->cap) {
		group->data = realloc(group->data, new_size * sizeof(struct pair));
		group->cap = new_size;
	}

	// combine enter queue
	int old_index = group->number - 1;
	int new_index = new_size - 1;
	for (i= enter - 1;i >=0 ; i--) {
		uint32_t handle = group->enter_queue.data[i];
		if (handle == last)
			continue;
		last = handle;
		struct skynet_context * ctx = skynet_handle_grab(handle);
		if (ctx == NULL)
			continue;
		if (old_index < 0) {
			group->data[new_index].handle = handle;
			group->data[new_index].ctx = ctx;
		} else {
			struct pair * p = &group->data[old_index];
			if (handle == p->handle)
				continue;
			if (handle > p->handle) {
				group->data[new_index].handle = handle;
				group->data[new_index].ctx = ctx;
			} else {
				group->data[new_index] = group->data[old_index];
				--old_index;
				last = 0;
				++i;
			}
		}
		--new_index;
	}
	while (old_index >= 0) {
		group->data[new_index] = group->data[old_index];
		--old_index;
		--new_index;
	}
	group->enter_queue.number = 0;

	// remove leave queue
	old_index = new_index + 1;
	new_index = 0;

	int count = new_size - old_index;

	int leave = group->leave_queue.number;
	for (i=0;i<leave;i++) {
		if (old_index >= new_size) {
			count = 0;
			break;
		}
		uint32_t handle = group->leave_queue.data[i];
		struct pair * p = &group->data[old_index];
		if (handle == p->handle) {
			--count;
			++old_index;
			if (p->ctx) {
				skynet_context_release(p->ctx);
			}
		} else if ( handle > p->handle) {
			group->data[new_index] = group->data[old_index];
			++new_index;
			++old_index;
			--i;
		} else {
			skynet_error(from, "Try to remove a none exist handle : %x", handle);
		}
	}
	while (new_index < count) {
		group->data[new_index] = group->data[old_index];
		++new_index;
		++old_index;
	}

	group->leave_queue.number = 0;

	group->number = new_index;
}

int 
skynet_multicast_castgroup(struct skynet_context * from, struct skynet_multicast_group * group, struct skynet_multicast_message *msg) {
	combine_queue(from, group);
	if (group->number == 0) {
		skynet_multicast_dispatch(msg, NULL, NULL);
		return 0;
	}
	uint32_t source = skynet_context_handle(from);
	skynet_multicast_copy(msg, group->number);
	int i;
	int release = 0;
	for (i=0;i<group->number;i++) {
		struct pair * p = &group->data[i];
		skynet_context_send(p->ctx, msg, 0 , source, SESSION_MULTICAST);
		int ref = skynet_context_ref(p->ctx);
		if (ref == 1) {
			skynet_context_release(p->ctx);
			struct skynet_context * ctx = skynet_handle_grab(p->handle);
			if (ctx == NULL) {
				p->ctx = NULL;
				skynet_multicast_leavegroup(group, p->handle);
				++release;
			}
		}
	}
	
	return group->number - release;
}
