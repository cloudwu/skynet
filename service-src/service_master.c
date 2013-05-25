#include "skynet.h"
#include "skynet_harbor.h"

#include <netinet/in.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdint.h>
#include <errno.h>

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
	int remote_fd[REMOTE_MAX];
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
	}
	memset(&m->map, 0, sizeof(m->map));
	return m;
}

void
master_release(struct master * m) {
	int i;
	for (i=0;i<REMOTE_MAX;i++) {
		int fd = m->remote_fd[i];
		if (fd >= 0) {
			close(fd);
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

static int
_connect_to(const char *ipaddress) {
	int fd = socket(AF_INET,SOCK_STREAM,0);
	struct sockaddr_in my_addr;
	char * port = strchr(ipaddress,':');
	if (port==NULL) {
		return -1;
	}
	int sz = port - ipaddress;
	char tmp[sz + 1];
	memcpy(tmp,ipaddress,sz);
	tmp[sz] = '\0';

	my_addr.sin_addr.s_addr=inet_addr(tmp);
	my_addr.sin_family=AF_INET;
	my_addr.sin_port=htons(strtol(port+1,NULL,10));

	int r = connect(fd,(struct sockaddr *)&my_addr,sizeof(struct sockaddr_in));

	if (r == -1) {
		close(fd);
		return -1;
	}
	
	return fd;
}

static int
_send_to(int fd, const void * buf, size_t sz, uint32_t handle) {
	char buffer[4 + sz + 12];
	uint32_t header = htonl(sz+12);
	memcpy(buffer, &header, 4);
	memcpy(buffer+4, buf, sz);
	uint32_t u32 = 0;
	memcpy(buffer+4+sz,&u32,4);
	u32 = htonl(handle);
	memcpy(buffer+4+sz+4,&u32,4);
	u32 = 0;
	memcpy(buffer+4+sz+8,&u32,4);

	sz += 4 + 12;

	for (;;) {
		int err = send(fd, buffer, sz, 0);
		if (err < 0) {
			switch (errno) {
			case EAGAIN:
			case EINTR:
				continue;
			}
		}
		if (err != sz) {
			return 1;
		}
		return 0;
	}
}

static void
_broadcast(struct skynet_context * context, struct master *m, const char *name, size_t sz, uint32_t handle) {
	int i;
	for (i=1;i<REMOTE_MAX;i++) {
		int fd = m->remote_fd[i];
		if (fd < 0)
			continue;
		int err = _send_to(fd, name, sz, handle);
		if (err) {
			close(fd);
			fd = _connect_to(m->remote_addr[i]);
			if (fd < 0) {
				m->remote_fd[i] = -1;
				skynet_error(context, "Reconnect to harbor %d : %s faild", i, m->remote_addr[i]);
			}
		}
	}
}

static void
_request_name(struct skynet_context * context, struct master *m, const char * buffer, size_t sz) {
	char name[GLOBALNAME_LENGTH];
	_copy_name(name, buffer, sz);
	struct name * n = _search_name(m, name);
	if (n == NULL) {
		return;
	}
	_broadcast(context, m, name, GLOBALNAME_LENGTH, n->value);
}

static void
_update_name(struct skynet_context * context, struct master *m, uint32_t handle, const char * buffer, size_t sz) {
	char name[GLOBALNAME_LENGTH];
	_copy_name(name, buffer, sz);
	struct name * n = _search_name(m, name);
	if (n==NULL) {
		n = _insert_name(m,name);
	}
	n->value = handle;
	_broadcast(context, m,name,GLOBALNAME_LENGTH, handle);
}

static void
_update_address(struct skynet_context * context, struct master *m, int harbor_id, const char * buffer, size_t sz) {
	if (m->remote_fd[harbor_id] >= 0) {
		close(m->remote_fd[harbor_id]);
		m->remote_fd[harbor_id] = -1;
	}
	free(m->remote_addr[harbor_id]);
	char * addr = malloc(sz+1);
	memcpy(addr, buffer, sz);
	addr[sz] = '\0';
	m->remote_addr[harbor_id] = addr;
	int fd = _connect_to(addr);
	if (fd<0) {
		skynet_error(context, "Can't connect to harbor %d : %s", harbor_id, addr);
		return;
	}
	m->remote_fd[harbor_id] = fd;
	_broadcast(context, m, addr, sz, harbor_id);

	int i;
	for (i=1;i<REMOTE_MAX;i++) {
		if (i == harbor_id)
			continue;
		const char * addr = m->remote_addr[i];
		if (addr == NULL) {
			continue;
		}
		_send_to(fd, addr, strlen(addr), i);
	}
}


/*
	update global name to master

	4 bytes (handle) (handle == 0 for request)
	n bytes string (name)
 */

static int
_mainloop(struct skynet_context * context, void * ud, int type, int session, uint32_t source, const void * msg, size_t sz) {
	assert(sz >= 4);

	if (type != PTYPE_HARBOR) {
		skynet_error(context, "None harbor message recv from %x (type = %d)", source, type);
		return 0;
	}
	struct master *m = ud;
	uint32_t handle = 0;
	memcpy(&handle, msg, 4);
	handle = ntohl(handle);
	sz -= 4;
	const char * name = msg;
	name += 4;

	if (handle == 0) {
		_request_name(context, m , name, sz);
	} else if (handle < REMOTE_MAX) {
		_update_address(context, m , handle, name, sz);
	} else {
		_update_name(context, m , handle, name, sz);
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

	return 0;
}
