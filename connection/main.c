#include "connection.h"
#include "skynet.h"


#include <sys/types.h>
#include <sys/socket.h>

#include <unistd.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#define DEFAULT_BUFFER_SIZE 1024
#define DEFAULT_CONNECTION 16

struct connection {
	int fd;
	uint32_t address;
	int close;
};

struct connection_server {
	int max_connection;
	int current_connection;
	struct connection_pool *pool;
	struct skynet_context *ctx;
	struct connection * conn;
};

struct connection_server *
connection_create(void) {
	struct connection_server * server = malloc(sizeof(*server));
	memset(server,0,sizeof(*server));
	return server;
}

void
connection_release(struct connection_server * server) {
	if (server->pool) {
		connection_deletepool(server->pool);
	}
	free(server->conn);
	free(server);
}

static void
_expand(struct connection_server * server) {
	connection_deletepool(server->pool);
	server->pool = connection_newpool(server->max_connection * 2);
	int i;
	for (i=0;i<server->max_connection;i++) {
		struct connection * c = &server->conn[i];
		int err = connection_add(server->pool, c->fd , c);
		assert(err == 0);
	}
	server->max_connection *= 2;
}

static void
_add(struct connection_server * server, int fd , uint32_t address) {
	++server->current_connection;
	if (server->current_connection > server->max_connection) {
		_expand(server);
	}
	int i;
	for (i=0;i<server->max_connection;i++) {
		struct connection * c = &server->conn[i];
		if (c->address == 0) {
			c->fd = fd;
			c->address = address;
			c->close = 0;
			int err = connection_add(server->pool, fd , c);
			assert(err == 0);
			return;
		}
	}
	assert(0);
}

static void
_del(struct connection_server * server, int fd) {
	int i;
	for (i=0;i<server->max_connection;i++) {
		struct connection * c = &server->conn[i];
		if (c->fd == fd) {
			if (c->close == 0) {
				skynet_send(server->ctx, 0, c->address, PTYPE_CLIENT | PTYPE_TAG_DONTCOPY, 0, NULL, 0);
				connection_del(server->pool, fd);
			}
			c->address = 0;
			c->fd = 0;
			c->close = 0;
			close(fd);
			return;
		}
	}

	skynet_error(server->ctx, "[connection] Delete invalid handle %d", fd);
}

static void
_poll(struct connection_server * server) {
	int timeout = 100;
	void * buffer = NULL;
	for (;;) {
		struct connection * c = connection_poll(server->pool, timeout);
		if (c==NULL) {
			skynet_command(server->ctx,"TIMEOUT","1");
			return;
		}
		timeout = 0;

		if (buffer == NULL) {
			buffer = malloc(DEFAULT_BUFFER_SIZE);
		}

		int size = read(c->fd, buffer, DEFAULT_BUFFER_SIZE);
		if (size < 0) {
			continue;
		}
		if (size == 0) {
			free(buffer);
			buffer = NULL;
			if (c->close == 0) {
				c->close = 1;
				skynet_send(server->ctx, 0, c->address, PTYPE_CLIENT | PTYPE_TAG_DONTCOPY, 0, NULL, 0);
				connection_del(server->pool, c->fd);
			}
		} else {
			skynet_send(server->ctx, 0, c->address, PTYPE_CLIENT | PTYPE_TAG_DONTCOPY, 0, buffer, size);
			buffer = NULL;
		}
	}
}

static int
_connection_main(struct skynet_context * ctx, void * ud, int type, int session, uint32_t source, const void * msg, size_t sz) {
	if (type == PTYPE_RESPONSE) {
		_poll(ud);
		return 0;
	}
	assert(type == PTYPE_TEXT);
	const char * param = (const char *)msg + 4;
	if (memcmp(msg, "ADD ", 4)==0) {
		char * endptr;
		int fd = strtol(param, &endptr, 10);
		if (endptr == NULL) {
			skynet_error(ctx, "[connection] Invalid ADD command from %x (session = %d)", source, session);
			return 0;
		}
		int addr_sz = sz - (endptr - (char *)msg);
		if (addr_sz <= 1) {
			skynet_error(ctx, "[connection] Invalid ADD command from %x (session = %d)", source, session);
			return 0;
		}
		char addr [addr_sz];
		memcpy(addr, endptr+1, addr_sz-1);
		addr[addr_sz-1] = '\0';
		uint32_t address = strtoul(addr+1, NULL, 16);
		if (address == 0) {
			skynet_error(ctx, "[connection] Invalid ADD command from %x (session = %d)", source, session);
			return 0;
		}
		_add(ud, fd, address);
	} else if (memcmp(msg, "DEL ", 4)==0) {
		char * endptr;
		int fd = strtol(param, &endptr, 10);
		if (endptr == NULL) {
			skynet_error(ctx, "[connection] Invalid DEL command from %x (session = %d)", source, session);
			return 0;
		}
		_del(ud, fd);
	} else {
		skynet_error(ctx, "[connection] Invalid command from %x (session = %d)", source, session);
	}

	return 0;
}

int
connection_init(struct connection_server * server, struct skynet_context * ctx, char * param) {
	server->pool = connection_newpool(DEFAULT_CONNECTION);
	if (server->pool == NULL)
		return 1;
	server->max_connection = strtol(param, NULL, 10);
	if (server->max_connection == 0) {
		server->max_connection = DEFAULT_CONNECTION;
	}
	server->current_connection = 0;
	server->ctx = ctx;
	server->conn = malloc(server->max_connection * sizeof(struct connection));
	memset(server->conn, 0, server->max_connection * sizeof(struct connection));

	skynet_callback(ctx, server, _connection_main);
	skynet_command(ctx,"REG",".connection");
	skynet_command(ctx,"TIMEOUT","0");
	return 0;
}



