#include "connection.h"
#include "skynet.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

struct reply {
	int session;
	uint32_t dest;
};

struct connection_server {
	int poll;
	int max_connection;
	struct connection_pool *pool;
	struct skynet_context *ctx;
	struct reply * reply;
};

typedef void (*command_func)(struct connection_server *server, const char * param, size_t sz, int session, const char * reply);

struct command {
	const char * name;
	command_func func;
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
	free(server->reply);
	free(server);
}

static inline const char * 
_command(const char * cmd, const char * msg, size_t sz) {
	int i;
	for (i=0;i<sz;i++) {
		if (cmd[i] == '\0') {
			if (msg[i] != ' ')
				return NULL;
			return msg+i+1;
		}
		if (cmd[i] != msg[i])
			return NULL;
	}
	return NULL;
}

static void
_connect(struct connection_server *server, const char * ipaddr, size_t sz, int session, const char * reply) {
	char tmp[sz+1];
	memcpy(tmp, ipaddr, sz);
	tmp[sz] = '\0';
	int id = connection_open(server->pool, ipaddr);
	if (id == 0) {
		skynet_send(server->ctx, reply, session, NULL, 0, 0);
		return;
	}
	char idstring[20];
	int n = sprintf(idstring, "%d", id);
	skynet_send(server->ctx, reply, session, idstring, n, 0);
}

static void
_close(struct connection_server *server, const char * param, size_t sz, int session, const char * reply) {
	int handle = strtol(param, NULL, 10);
	if (handle <= 0) {
		skynet_error(server->ctx, "[connection] Close invalid handle from %s", reply);
		return;
	}
	connection_close(server->pool, handle);
}

static void
_write(struct connection_server *server, const char * param, size_t sz, int session, const char * reply) {
	char * endptr = NULL;
	int handle = strtol(param, &endptr, 10);
	if (handle <= 0 || endptr == NULL || *endptr !=' ') {
		skynet_error(server->ctx, "[connection] Write invalid handle from %s", reply);
		return;
	}
	++endptr;
	sz -= endptr - param;
	connection_write(server->pool, handle, endptr, sz);
}

static void
_read(struct connection_server *server, const char * param, size_t sz, int session, const char * reply) {
	char * endptr = NULL;
	int handle = strtol(param, &endptr, 10);
	if (handle <= 0 || endptr == NULL || *endptr !=' ') {
		skynet_error(server->ctx, "[connection] Read invalid handle from %s", reply);
		return;
	}
	int size = strtol(endptr+1, &endptr, 10);

	if (size <= 0 || endptr == NULL) {
		skynet_error(server->ctx, "[connection] Read invalid size (%d) from %s", size, reply);
		return;
	}
	void * buffer = connection_read(server->pool, handle, size);
	if (buffer == NULL) {
		int id = connection_id(server->pool, handle);
		if (id == 0) {
			skynet_send(server->ctx, reply, session, NULL, 0, 0);
			return;
		}
		--id;
		assert(id < server->max_connection);
		assert(reply[0] == ':');
		server->reply[id].session = session;
		server->reply[id].dest = strtoul(reply+1, NULL, 16); 
		++server->poll;
		if (server->poll == 1) {
			skynet_command(server->ctx, "TIMEOUT","0");
			return;
		}
	} else {
		skynet_send(server->ctx, reply, session, buffer, size, 0);
	}
}

static void
_readline(struct connection_server *server, const char * param, size_t sz, int session, const char * reply) {
	char * endptr = NULL;
	int handle = strtol(param, &endptr, 10);
	if (handle <= 0 || endptr == NULL || *endptr !=' ') {
		skynet_error(server->ctx, "[connection] Readline invalid handle from %s", reply);
		return;
	}

	sz -= endptr - param + 1;
	if (sz < 1 || sz > 7) {
		skynet_error(server->ctx, "[connection] Readline invalid sep (size = %d) from %s", sz, reply);
		return;
	}

	char sep[sz+1];
	memcpy(sep, endptr+1, sz);
	sep[sz] = '\0';

	void * buffer = connection_readline(server->pool, handle, sep, &sz);
	if (buffer == NULL) {
		int id = connection_id(server->pool, handle);
		if (id == 0) {
			skynet_send(server->ctx, reply, session, NULL, 0, 0);
			return;
		}
		--id;
		assert(id < server->max_connection);
		assert(reply[0] == ':');
		server->reply[id].session = session;
		server->reply[id].dest = strtoul(reply+1, NULL, 16); 
		++server->poll;
		if (server->poll == 1) {
			skynet_command(server->ctx, "TIMEOUT","0");
			return;
		}
	} else {
		skynet_send(server->ctx, reply, session, buffer, sz, 0);
	}
}

static void
_id_to_hex(char * str, uint32_t id) {
	int i;
	static char hex[16] = { '0','1','2','3','4','5','6','7','8','9','A','B','C','D','E','F' };
	for (i=0;i<8;i++) {
		str[i] = hex[(id >> ((7-i) * 4))&0xf];
	}
	str[8] = '\0';
}

static void
_poll(struct connection_server *server) {
	int handle = 0;
	size_t sz = 0;
	int timeout = 100;
	char addr[10];
	for (;;) {
		void * buffer = connection_poll(server->pool, timeout, &handle, &sz);
		timeout = 0;
		if (buffer == NULL) {
			if (handle) {
				--server->poll;
				int id = sz;
				struct reply * r = &server->reply[id];
				addr[0] = ':';
				_id_to_hex(addr+1, id);
				skynet_send(server->ctx, addr , r->session, NULL, 0, 0);
			} else {
				assert(server->poll >= 0);
				if (server->poll > 0) {
					skynet_command(server->ctx, "TIMEOUT","1");
				}
				return;
			}
		} else {
			--server->poll;
			int id = connection_id(server->pool, handle);
			assert(id > 0);
			--id;
			struct reply * r = &server->reply[id];
			addr[0] = ':';
			_id_to_hex(addr+1, r->dest);
			skynet_send(server->ctx, addr, r->session, buffer, sz, 0);
		}
	}
}

static void
_main(struct skynet_context * ctx, void * ud, int session, const char * uid, const void * msg, size_t sz) {
	if (msg == NULL) {
		assert(session >= 0);
		_poll(ud);
		return;
	}
	if (session > 0) {
		skynet_error(ctx, "[connection] Invalid response (session = %d) from %s", session, uid);
		return;
	}
	struct command cmd[] = {
		{ "CONNECT", _connect },
		{ "CLOSE" , _close },
		{ "READ" , _read },
		{ "READLINE", _readline },
		{ "WRITE", _write },
		{ NULL, NULL },
	};

	struct command * p = cmd;
	while (p->name) {
		const char * param = _command(p->name, msg, sz);
		if (param) {
			p->func(ud, param, sz - (param-(const char *)msg), -session, uid);
			return;
		}
		++p;
	}

	skynet_error(ctx, "[connection] Invalid command from %s (session = %d)", uid, -session);
}

int
connection_init(struct connection_server * server, struct skynet_context * ctx, char * param) {
	int max = strtol(param, NULL, 10);
	if (max <=0) {
		return 1;
	}
	server->pool = connection_newpool(max);
	if (server->pool == NULL)
		return 1;
	server->max_connection = max;
	server->reply = malloc(sizeof(struct reply) * max);
	memset(server->reply, 0, sizeof(struct reply) * max);
	server->ctx = ctx;
	server->poll = 0;
	skynet_callback(ctx, server, _main);
	skynet_command(ctx,"REG",".connection");
	return 0;
}



