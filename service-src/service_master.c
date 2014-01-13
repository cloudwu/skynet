#include "skynet.h"
#include "skynet_harbor.h"
#include "skynet_socket.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <assert.h>
#include <stdint.h>

#define HASH_SIZE 4096

struct name {
	struct name * next;
	char key[GLOBALNAME_LENGTH];
	uint32_t hash;
	uint32_t value;
};

struct namemap {
	struct name *node[HASH_SIZE];
};

struct master {
	struct skynet_context *ctx;
	int remote_fd[REMOTE_MAX];
	bool connected[REMOTE_MAX];
	char * remote_addr[REMOTE_MAX];
	struct namemap map;
};

struct master *
master_create() {
	struct master *m = malloc(sizeof(*m));
	int i;
	for (i=0;i<REMOTE_MAX;i++) {
		m->remote_fd[i] = -1;
		m->remote_addr[i] = NULL;
		m->connected[i] = false;
	}
	memset(&m->map, 0, sizeof(m->map));
	return m;
}

void
master_release(struct master * m) {
	int i;
	struct skynet_context *ctx = m->ctx;
	for (i=0;i<REMOTE_MAX;i++) {
		int fd = m->remote_fd[i];
		if (fd >= 0) {
			assert(ctx);
			skynet_socket_close(ctx, fd);
		}
		free(m->remote_addr[i]);
	}
	for (i=0;i<HASH_SIZE;i++) {
		struct name * node = m->map.node[i];
		while (node) {
			struct name * next = node->next;
			free(node);
			node = next;
		}
	}
	free(m);
}

static struct name *
_search_name(struct master *m, char name[GLOBALNAME_LENGTH]) {
	uint32_t *ptr = (uint32_t *) name;
	uint32_t h = ptr[0] ^ ptr[1] ^ ptr[2] ^ ptr[3];
	struct name * node = m->map.node[h % HASH_SIZE];
	while (node) {
		if (node->hash == h && strncmp(node->key, name, GLOBALNAME_LENGTH) == 0) {
			return node;
		}
		node = node->next;
	}
	return NULL;
}

static struct name *
_insert_name(struct master *m, char name[GLOBALNAME_LENGTH]) {
	uint32_t *ptr = (uint32_t *)name;
	uint32_t h = ptr[0] ^ ptr[1] ^ ptr[2] ^ ptr[3];
	struct name **pname = &m->map.node[h % HASH_SIZE];
	struct name * node = malloc(sizeof(*node));
	memcpy(node->key, name, GLOBALNAME_LENGTH);
	node->next = *pname;
	node->hash = h;
	node->value = 0;
	*pname = node;
	return node;
}

static void
_copy_name(char *name, const char * buffer, size_t sz) {
	if (sz < GLOBALNAME_LENGTH) {
		memcpy(name, buffer, sz);
		memset(name+sz, 0 , GLOBALNAME_LENGTH - sz);
	} else {
		memcpy(name, buffer, GLOBALNAME_LENGTH);
	}
}

static void
_connect_to(struct master *m, int id) {
	assert(m->connected[id] == false);
	struct skynet_context * ctx = m->ctx;
	const char *ipaddress = m->remote_addr[id];
	char * portstr = strchr(ipaddress,':');
	if (portstr==NULL) {
		skynet_error(ctx, "Harbor %d : address invalid (%s)",id, ipaddress);
		return;
	}
	int sz = portstr - ipaddress;
	char tmp[sz + 1];
	memcpy(tmp,ipaddress,sz);
	tmp[sz] = '\0';
	int port = strtol(portstr+1,NULL,10);
	skynet_error(ctx, "Master connect to harbor(%d) %s:%d", id, tmp, port);
	m->remote_fd[id] = skynet_socket_connect(ctx, tmp, port);
}

static inline void
to_bigendian(uint8_t *buffer, uint32_t n) {
	buffer[0] = (n >> 24) & 0xff;
	buffer[1] = (n >> 16) & 0xff;
	buffer[2] = (n >> 8) & 0xff;
	buffer[3] = n & 0xff;
}

static void
_send_to(struct master *m, int id, const void * buf, int sz, uint32_t handle) {
	uint8_t * buffer= (uint8_t *)malloc(4 + sz + 12);
	to_bigendian(buffer, sz+12);
	memcpy(buffer+4, buf, sz);
	to_bigendian(buffer+4+sz, 0);
	to_bigendian(buffer+4+sz+4, handle);
	to_bigendian(buffer+4+sz+8, 0);

	sz += 4 + 12;

	if (skynet_socket_send(m->ctx, m->remote_fd[id], buffer, sz)) {
		skynet_error(m->ctx, "Harbor %d : send error", id);
	}
}

static void
_broadcast(struct master *m, const char *name, size_t sz, uint32_t handle) {
	int i;
	for (i=1;i<REMOTE_MAX;i++) {
		int fd = m->remote_fd[i];
		if (fd < 0 || m->connected[i]==false)
			continue;
		_send_to(m, i , name, sz, handle);
	}
}

static void
_request_name(struct master *m, const char * buffer, size_t sz) {
	char name[GLOBALNAME_LENGTH];
	_copy_name(name, buffer, sz);
	struct name * n = _search_name(m, name);
	if (n == NULL) {
		return;
	}
	_broadcast(m, name, GLOBALNAME_LENGTH, n->value);
}

static void
_update_name(struct master *m, uint32_t handle, const char * buffer, size_t sz) {
	char name[GLOBALNAME_LENGTH];
	_copy_name(name, buffer, sz);
	struct name * n = _search_name(m, name);
	if (n==NULL) {
		n = _insert_name(m,name);
	}
	n->value = handle;
	_broadcast(m,name,GLOBALNAME_LENGTH, handle);
}

static void
close_harbor(struct master *m, int harbor_id) {
	if (m->connected[harbor_id]) {
		struct skynet_context * context = m->ctx;
		skynet_socket_close(context, m->remote_fd[harbor_id]);
		m->remote_fd[harbor_id] = -1;
		m->connected[harbor_id] = false;
	}
}

static void
_update_address(struct master *m, int harbor_id, const char * buffer, size_t sz) {
	if (m->remote_fd[harbor_id] >= 0) {
		close_harbor(m, harbor_id);
	}
	free(m->remote_addr[harbor_id]);
	char * addr = malloc(sz+1);
	memcpy(addr, buffer, sz);
	addr[sz] = '\0';
	m->remote_addr[harbor_id] = addr;
	_connect_to(m, harbor_id);
}

static int
socket_id(struct master *m, int id) {
	int i;
	for (i=1;i<REMOTE_MAX;i++) {
		if (m->remote_fd[i] == id)
			return i;
	}
	return 0;
}

static void
on_connected(struct master *m, int id) {
	_broadcast(m, m->remote_addr[id], strlen(m->remote_addr[id]), id);
	m->connected[id] = true;
	int i;
	for (i=1;i<REMOTE_MAX;i++) {
		if (i == id)
			continue;
		const char * addr = m->remote_addr[i];
		if (addr == NULL || m->connected[i] == false) {
			continue;
		}
		_send_to(m, id , addr, strlen(addr), i);
	}
}

static void
dispatch_socket(struct master *m, const struct skynet_socket_message *msg, int sz) {
	int id = socket_id(m, msg->id);
	switch(msg->type) {
	case SKYNET_SOCKET_TYPE_CONNECT:
		assert(id);
		on_connected(m, id);
		break;
	case SKYNET_SOCKET_TYPE_ERROR:
		skynet_error(m->ctx, "socket error on harbor %d", id);
		// go though, close socket
	case SKYNET_SOCKET_TYPE_CLOSE:
		close_harbor(m, id);
		break;
	default:
		skynet_error(m->ctx, "Invalid socket message type %d", msg->type);
		break;
	}
}


/*
	update global name to master

	4 bytes (handle) (handle == 0 for request)
	n bytes string (name)
 */

static int
_mainloop(struct skynet_context * context, void * ud, int type, int session, uint32_t source, const void * msg, size_t sz) {
	if (type == PTYPE_SOCKET) {
		dispatch_socket(ud, msg, (int)sz);
		return 0;
	}
	if (type != PTYPE_HARBOR) {
		skynet_error(context, "None harbor message recv from %x (type = %d)", source, type);
		return 0;
	}
	assert(sz >= 4);
	struct master *m = ud;
	const uint8_t *handlen = msg;
	uint32_t handle = handlen[0]<<24 | handlen[1]<<16 | handlen[2]<<8 | handlen[3];
	sz -= 4;
	const char * name = msg;
	name += 4;

	if (handle == 0) {
		_request_name(m , name, sz);
	} else if (handle < REMOTE_MAX) {
		_update_address(m , handle, name, sz);
	} else {
		_update_name(m , handle, name, sz);
	}

	return 0;
}

int
master_init(struct master *m, struct skynet_context *ctx, const char * args) {
	char tmp[strlen(args) + 32];
	sprintf(tmp,"gate L ! %s %d %d 0",args,PTYPE_HARBOR,REMOTE_MAX);
	const char * gate_addr = skynet_command(ctx, "LAUNCH", tmp);
	if (gate_addr == NULL) {
		skynet_error(ctx, "Master : launch gate failed");
		return 1;
	}
	uint32_t gate = strtoul(gate_addr+1, NULL, 16);
	if (gate == 0) {
		skynet_error(ctx, "Master : launch gate invalid %s", gate_addr);
		return 1;
	}
	const char * self_addr = skynet_command(ctx, "REG", NULL);
	int n = sprintf(tmp,"broker %s",self_addr);
	skynet_send(ctx, 0, gate, PTYPE_TEXT, 0, tmp, n);
	skynet_send(ctx, 0, gate, PTYPE_TEXT, 0, "start", 5);

	skynet_callback(ctx, m, _mainloop);

	m->ctx = ctx;
	return 0;
}
