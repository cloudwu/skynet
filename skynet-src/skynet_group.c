#include "skynet_group.h"
#include "skynet_multicast.h"
#include "skynet_server.h"
#include "skynet.h"

#include <string.h>
#include <stdio.h>
#include <assert.h>

#define HASH_SIZE 1024

struct group_node {
	int handle;
	struct skynet_context *ctx;
	struct group_node * next;
};

struct group {
	int lock;
	struct group_node * node[HASH_SIZE];
};

struct group * _G = NULL;

inline static void
_lock(struct group *g) {
	while (__sync_lock_test_and_set(&g->lock,1)) {}
}

inline static void
_unlock(struct group *g) {
	__sync_lock_release(&g->lock);
}

static struct skynet_context *
_create_group(struct group * g, int handle) {
	int hash = handle % HASH_SIZE;
	struct skynet_context * inst = skynet_context_new("multicast",NULL);
	assert(inst);
	struct group_node * new_node = malloc(sizeof(struct group_node));
	new_node->handle = handle;
	new_node->ctx = inst;
	new_node->next = g->node[hash];
	g->node[hash] = new_node;

	return inst;
}

uint32_t
skynet_group_query(int handle) {
	struct group *g = _G;
	_lock(g);
	
	int hash = handle % HASH_SIZE;
	struct group_node * node = g->node[hash];
	while (node) {
		if (node->handle == handle) {
			struct skynet_context * ctx = node->ctx;
			uint32_t addr = skynet_context_handle(ctx);
			_unlock(g);
			return addr;
		}
		node = node->next;
	}
	struct skynet_context * ctx = _create_group(g, handle);
	uint32_t addr = skynet_context_handle(ctx);
	_unlock(g);

	return addr;
}

static void
send_command(struct skynet_context *ctx, const char * cmd, uint32_t node) {
	char * tmp = malloc(16);
	int n = sprintf(tmp, "%s %x", cmd, node);
	skynet_context_send(ctx, tmp, n+1 , 0, PTYPE_SYSTEM, 0);
}

void 
skynet_group_enter(int handle, uint32_t n) {
	struct group *g = _G;
	_lock(g);

	int hash = handle % HASH_SIZE;
	struct group_node * node = g->node[hash];
	while (node) {
		if (node->handle == handle) {
			send_command(node->ctx, "E", n);
			_unlock(g);
			return;
		}
		node = node->next;
	}
	struct skynet_context * inst = _create_group(g, handle);

	send_command(inst, "E", n);

	_unlock(g);
}

void 
skynet_group_leave(int handle, uint32_t n) {
	struct group *g = _G;
	_lock(g);

	int hash = handle % HASH_SIZE;
	struct group_node * node = g->node[hash];
	while (node) {
		if (node->handle == handle) {
			send_command(node->ctx, "L", n);
			break;
		}
		node = node->next;
	}

	_unlock(g);
}

void
skynet_group_clear(int handle) {
	struct group *g = _G;
	_lock(g);

	int hash = handle % HASH_SIZE;
	struct group_node ** pnode = &g->node[hash];
	while (*pnode) {
		struct group_node * node = *pnode;
		if (node->handle == handle) {
			struct skynet_context * ctx = node->ctx;
			
			char * cmd = malloc(8);
			int n = sprintf(cmd, "C");
			skynet_context_send(ctx, cmd, n+1, 0 , PTYPE_SYSTEM, 0);
			*pnode = node->next;
			free(node);
			break;
		}
		pnode = &node->next;
	}

	_unlock(g);
}

void 
skynet_group_init() {
	struct group * g = malloc(sizeof(*g));
	memset(g,0,sizeof(*g));
	_G = g;
}
