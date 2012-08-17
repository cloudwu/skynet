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
	char * addr;
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
	int i;
	for (i=0;i<server->max_connection;i++) {
		free(server->conn[i].addr);
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
		connection_add(server->pool, c->fd , c);
	}
	server->max_connection *= 2;
}

static void
_add(struct connection_server * server, int fd , char * addr) {
	++server->current_connection;
	if (server->current_connection > server->max_connection) {
		_expand(server);
	}
	int i;
	for (i=0;i<server->max_connection;i++) {
		struct connection * c = &server->conn[i];
		if (c->addr == NULL) {
			c->fd = fd;
			c->addr = addr;
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
			free(c->addr);
			c->addr = NULL;
			c->fd = 0;
			connection_del(server->pool, fd);
			return;
		}
	}

	skynet_error(server->ctx, "[connection] Delete invalid handle %d", fd);
}

static void
_poll(struct connection_server * server) {
	int timeout = 100;
	for (;;) {
		struct connection * c = connection_poll(server->pool, timeout);
		if (c==NULL) {
			skynet_command(server->ctx,"TIMEOUT","1");
			return;
		}
		timeout = 0;

		void * buffer = malloc(DEFAULT_BUFFER_SIZE);

		int size = recv(c->fd, buffer, DEFAULT_BUFFER_SIZE, MSG_DONTWAIT);
		if (size < 0) {
			continue;
		}
		if (size == 0) {
			connection_del(server->pool, c->fd);
			free(buffer);
			skynet_send(server->ctx, NULL, c->addr, 0x7fffffff, NULL, 0, DONTCOPY);
		} else {
			skynet_send(server->ctx, NULL, c->addr, 0x7fffffff, buffer, size, DONTCOPY);
		}
	}
}

static int
_main(struct skynet_context * ctx, void * ud, int session, const char * uid, const void * msg, size_t sz) {
	if (msg == NULL) {
		_poll(ud);
		return 0;
	}
	const char * param = (const char *)msg + 4;
	if (memcmp(msg, "ADD ", 4)==0) {
		char * endptr;
		int fd = strtol(param, &endptr, 10);
		if (endptr == NULL) {
			skynet_error(ctx, "[connection] Invalid ADD command from %s (session = %d)", uid, session);
			return 0;
		}
		int addr_sz = sz - (endptr - (char *)msg);
		char * addr = malloc(addr_sz);
		memcpy(addr, endptr+1, addr_sz-1);
		addr[addr_sz-1] = '\0';
		_add(ud, fd, addr);
	} else if (memcmp(msg, "DEL ", 4)==0) {
		char * endptr;
		int fd = strtol(param, &endptr, 10);
		if (endptr == NULL) {
			skynet_error(ctx, "[connection] Invalid DEL command from %s (session = %d)", uid, session);
			return 0;
		}
		_del(ud, fd);
	} else {
		skynet_error(ctx, "[connection] Invalid command from %s (session = %d)", uid, session);
	}

	return 0;
}

int
connection_init(struct connection_server * server, struct skynet_context * ctx, char * param) {
	server->pool = connection_newpool(DEFAULT_CONNECTION);
	if (server->pool == NULL)
		return 1;
	server->max_connection = DEFAULT_CONNECTION;
	server->current_connection = 0;
	server->ctx = ctx;
	server->conn = malloc(server->max_connection * sizeof(struct connection));
	memset(server->conn, 0, server->max_connection * sizeof(struct connection));

	skynet_callback(ctx, server, _main);
	skynet_command(ctx,"REG",".connection");
	skynet_command(ctx,"TIMEOUT","0");
	return 0;
}



