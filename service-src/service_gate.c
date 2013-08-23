#include "skynet.h"
#include "skynet_socket.h"

#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdarg.h>

#define BACKLOG 32
#define MESSAGEPOOL 1024

struct message {
	char * buffer;
	int size;
	struct message * next;
};

struct connection {
	int id;	// skynet_socket id
	struct connection * next;
	uint32_t agent;
	uint32_t client;
	char remote_name[32];
	int header;
	int offset;
	int size;
	struct message * head;
	struct message * tail;
};

struct gate {
	int listen_id;
	uint32_t watchdog;
	uint32_t broker;
	int hashmod;
	int connection;
	int max_connection;
	int client_tag;
	int header_size;
	struct connection ** hash;
	struct connection * pool;
	// todo: save message pool ptr for release
	struct message * freelist;
};

struct gate *
gate_create(void) {
	struct gate * g = malloc(sizeof(*g));
	memset(g,0,sizeof(*g));
	g->listen_id = -1;
	return g;
}

void
gate_release(struct gate *g) {
	// todo: close all the fd and free memory (be careful about freelist)
}

static inline void
return_message(struct gate *g, struct connection *c) {
	struct message *m = c->head;
	if (m->next == NULL) {
		assert(c->tail == m);
		c->head = c->tail = NULL;
	} else {
		c->head = m->next;
	}
	free(m->buffer);
	m->buffer = NULL;
	m->size = 0;
	m->next = g->freelist;
	g->freelist = m;
}

static struct connection *
lookup_id(struct gate *g, int id) {
	int h = id & g->hashmod;
	struct connection * c = g->hash[h];
	while(c) {
		if (c->id == id)
			return c;
		c = c->next;
	}
	return NULL;
}

static struct connection *
remove_id(struct gate *g, int id) {
	int h = id & g->hashmod;
	struct connection * c = g->hash[h];
	if (c == NULL)
		return NULL;
	if (c->id == id) {
		g->hash[h] = c->next;
		goto _clear;
	}
	while(c->next) {
		if (c->next->id == id) {
			struct connection * temp = c;
			c->next = temp->next;
			c = temp;
			goto _clear;
		}
		c = c->next;
	}
	return NULL;
_clear:
	while (c->head) {
		return_message(g,c);
	}
	memset(c, 0, sizeof(*c));
	c->id = -1;
	--g->connection;
	return c;
}

static struct connection *
insert_id(struct gate *g, int id) {
	struct connection *c = NULL;
	int i;
	for (i=0;i<g->max_connection;i++) {
		int index = (i+g->connection) % g->max_connection;
		if (g->pool[index].id == -1) {
			c = &g->pool[index];
			break;
		}
	}
	assert(c);
	c->id = id;
	assert(c->next == NULL);
	int h = id & g->hashmod;
	if (g->hash[h]) {
		c->next = g->hash[h];
	} else {
		g->hash[h] = c;
	}
	return c;
}

static void
_parm(char *msg, int sz, int command_sz) {
	while (command_sz < sz) {
		if (msg[command_sz] != ' ')
			break;
		++command_sz;
	}
	int i;
	for (i=command_sz;i<sz;i++) {
		msg[i-command_sz] = msg[i];
	}
	msg[i-command_sz] = '\0';
}

static void
_forward_agent(struct gate * g, int id, uint32_t agentaddr, uint32_t clientaddr) {
	struct connection * agent = lookup_id(g,id);
	if (agent) {
		agent->agent = agentaddr;
		agent->client = clientaddr;
	}
}

static void
_ctrl(struct skynet_context * ctx, struct gate * g, const void * msg, int sz) {
	char tmp[sz+1];
	memcpy(tmp, msg, sz);
	tmp[sz] = '\0';
	char * command = tmp;
	int i;
	if (sz == 0)
		return;
	for (i=0;i<sz;i++) {
		if (command[i]==' ') {
			break;
		}
	}
	if (memcmp(command,"kick",i)==0) {
		_parm(tmp, sz, i);
		int uid = strtol(command , NULL, 10);
		struct connection * agent = lookup_id(g,uid);
		if (agent) {
			skynet_socket_close(ctx, uid);
		}
		return;
	}
	if (memcmp(command,"forward",i)==0) {
		_parm(tmp, sz, i);
		char * client = tmp;
		char * idstr = strsep(&client, " ");
		if (client == NULL) {
			return;
		}
		int id = strtol(idstr , NULL, 10);
		char * agent = strsep(&client, " ");
		if (client == NULL) {
			return;
		}
		uint32_t agent_handle = strtoul(agent+1, NULL, 16);
		uint32_t client_handle = strtoul(client+1, NULL, 16);
		_forward_agent(g, id, agent_handle, client_handle);
		return;
	}
	if (memcmp(command,"broker",i)==0) {
		_parm(tmp, sz, i);
		g->broker = skynet_queryname(ctx, command);
		return;
	}
	if (memcmp(command,"start",i) == 0) {
		skynet_socket_start(ctx, g->listen_id);
		return;
	}
    if (memcmp(command, "close", i) == 0) {
		if (g->listen_id >= 0) {
			skynet_socket_close(ctx, g->listen_id);
		}
		return;
	}
	skynet_error(ctx, "[gate] Unkown command : %s", command);
}

static void
_report(struct gate *g, struct skynet_context * ctx, const char * data, ...) {
	if (g->watchdog == 0) {
		return;
	}
	va_list ap;
	va_start(ap, data);
	char tmp[1024];
	int n = vsnprintf(tmp, sizeof(tmp), data, ap);
	va_end(ap);

	skynet_send(ctx, 0, g->watchdog, PTYPE_TEXT,  0, tmp, n);
}

static void
read_data(struct gate *g, struct connection *c, char * buffer, int sz) {
	assert(c->size >= sz);
	c->size -= sz;
	for (;;) {
		struct message *current = c->head;
		int bsz = current->size - c->offset;
		if (bsz > sz) {
			memcpy(buffer, current->buffer + c->offset, sz);
			c->offset += sz;
			return;
		}
		if (bsz == sz) {
			memcpy(buffer, current->buffer + c->offset, sz);
			c->offset = 0;
			return_message(g, c);
			return;
		} else {
			memcpy(buffer, current->buffer + c->offset, bsz);
			return_message(g, c);
			c->offset = 0;
			buffer+=bsz;
			sz-=bsz;
		}
	}
}

static void
_forward(struct skynet_context * ctx,struct gate *g, struct connection * c) {
	if (g->broker) {
		void * temp = malloc(c->header);
		read_data(g,c,temp, c->header);
		skynet_send(ctx, 0, g->broker, g->client_tag | PTYPE_TAG_DONTCOPY, 0, temp, c->header);
		return;
	}
	if (c->agent) {
		void * temp = malloc(c->header);
		read_data(g,c,temp, c->header);
		skynet_send(ctx, c->client, c->agent, g->client_tag | PTYPE_TAG_DONTCOPY, 0 , temp, c->header);
	} else if (g->watchdog) {
		char * tmp = malloc(c->header + 32);
		int n = snprintf(tmp,32,"%d data ",c->id);
		read_data(g,c,tmp+n,c->header);
		skynet_send(ctx, 0, g->watchdog, PTYPE_TEXT | PTYPE_TAG_DONTCOPY, 0, tmp, c->header + n);
	}
}

static void
push_message(struct gate *g, struct connection *c, void * data, int sz) {
	struct message * m;
	if (g->freelist) {
		m = g->freelist;
		g->freelist = m->next;
	} else {
		struct message * temp = malloc(sizeof(struct message) * MESSAGEPOOL);
		int i;
		for (i=1;i<MESSAGEPOOL;i++) {
			temp[i].buffer = NULL;
			temp[i].size = 0;
			temp[i].next = &temp[i+1];
		}
		temp[MESSAGEPOOL-1].next = NULL;
		m = temp;
		g->freelist = &temp[1];
	}
	m->buffer = data;
	m->size = sz;
	m->next = NULL;
	c->size += sz;
	if (c->head == NULL) {
		assert(c->tail == NULL);
		c->head = c->tail = m;
	} else {
		c->tail->next = m;
		c->tail = m;
	}
}

static void
dispatch_message(struct skynet_context *ctx, struct gate *g, struct connection *c, int id, void * data, int sz) {
	push_message(g, c, data, sz);
	if (c->header == 0) {
		// parser header (2 or 4)
		if (c->size < g->header_size) {
			return;
		}
		uint8_t plen[4];
		read_data(g,c,(char *)plen,g->header_size);
		// big-endian
		if (g->header_size == 2) {
			c->header = plen[0] << 8 | plen[1];
		} else {
			c->header = plen[0] << 24 | plen[1] << 16 | plen[2] << 8 | plen[3];
		}
		if (c->header == 0) {
			// empty message (invalid), not forwarding
			return;
		}
	}
	if (c->size < c->header)
		return;
	_forward(ctx, g, c);
	c->header = 0;
}

static void
dispatch_socket_message(struct skynet_context * ctx, struct gate *g, const struct skynet_socket_message * message, int sz) {
	switch(message->type) {
	case SKYNET_SOCKET_TYPE_DATA: {
		struct connection *c = lookup_id(g, message->id);
		if (c) {
			dispatch_message(ctx, g, c, message->id, message->buffer, message->ud);
		} else {
			skynet_error(ctx, "Drop unknown connection %d message", message->id);
			skynet_socket_close(ctx, message->id);
			free(message->buffer);
		}
		break;
	}
	case SKYNET_SOCKET_TYPE_CONNECT: {
		if (message->id == g->listen_id) {
			// start listening
			break;
		}
		struct connection *c = lookup_id(g, message->id);
		if (c) {
			_report(g, ctx, "%d open %d %s:0",message->id,message->id,c->remote_name);
		} else {
			skynet_error(ctx, "Close unknown connection %d", message->id);
			skynet_socket_close(ctx, message->id);
		}
		break;
	}
	case SKYNET_SOCKET_TYPE_CLOSE:
	case SKYNET_SOCKET_TYPE_ERROR: {
		struct connection * c = remove_id(g, message->id);
		if (c) {
			_report(g, ctx, "%d close", message->id);
		}
		break;
	}
	case SKYNET_SOCKET_TYPE_ACCEPT:
		// report accept, then it will be get a SKYNET_SOCKET_TYPE_CONNECT message
		assert(g->listen_id == message->id);
		if (g->connection >= g->max_connection) {
			skynet_socket_close(ctx, message->ud);
		} else {
			++g->connection;
			struct connection *c = insert_id(g, message->ud);
			if (sz >= sizeof(c->remote_name)) {
				sz = sizeof(c->remote_name) - 1;
			}
			memcpy(c->remote_name, message+1, sz);
			c->remote_name[sz] = '\0';
			skynet_socket_start(ctx, message->ud);
		}
		break;
	}
}

static int
_cb(struct skynet_context * ctx, void * ud, int type, int session, uint32_t source, const void * msg, size_t sz) {
	struct gate *g = ud;
	switch(type) {
	case PTYPE_TEXT:
		_ctrl(ctx, g , msg , (int)sz);
		break;
	case PTYPE_CLIENT: {
		if (sz <=4 ) {
			skynet_error(ctx, "Invalid client message from %x",source);
			break;
		}
		// The last 4 bytes in msg are the id of socket, write following bytes to it
		const uint8_t * idbuf = msg + sz - 4;
		uint32_t uid = idbuf[0] | idbuf[1] << 8 | idbuf[2] << 16 | idbuf[3] << 24;
		struct connection * c = lookup_id(g,uid);
		if (c) {
			// don't send id (last 4 bytes)
			skynet_socket_send(ctx, uid, (void*)msg, sz-4);
			// return 1 means don't free msg
			return 1;
		} else {
			skynet_error(ctx, "Invalid client id %d from %x",(int)uid,source);
			break;
		}
	}
	case PTYPE_SOCKET:
		assert(source == 0);
		// recv socket message from skynet_socket
		dispatch_socket_message(ctx, g, msg, (int)(sz-sizeof(struct skynet_socket_message)));
		break;
	}
	return 0;
}

static void
start_listen(struct skynet_context * ctx, struct gate *g, char * listen_addr) {
	char * portstr = strchr(listen_addr,':');
	const char * host = "";
	int port;
	if (portstr == NULL) {
		port = strtol(listen_addr, NULL, 10);
		if (port <= 0) {
			skynet_error(ctx, "Invalid gate address %s",listen_addr);
			return;
		}
	} else {
		port = strtol(portstr + 1, NULL, 10);
		if (port <= 0) {
			skynet_error(ctx, "Invalid gate address %s",listen_addr);
			return;
		}
		portstr[0] = '\0';
		host = listen_addr;
	}
	g->listen_id = skynet_socket_listen(ctx, host, port, BACKLOG);
}

int
gate_init(struct gate *g , struct skynet_context * ctx, char * parm) {
	int max = 0;
	int buffer = 0;
	int sz = strlen(parm)+1;
	char watchdog[sz];
	char binding[sz];
	int client_tag = 0;
	char header;
	int n = sscanf(parm, "%c %s %s %d %d %d",&header,watchdog, binding,&client_tag , &max,&buffer);
	if (n<4) {
		skynet_error(ctx, "Invalid gate parm %s",parm);
		return 1;
	}
	if (max <=0 ) {
		skynet_error(ctx, "Need max connection");
		return 1;
	}
	if (header != 'S' && header !='L') {
		skynet_error(ctx, "Invalid data header style");
		return 1;
	}

	if (client_tag == 0) {
		client_tag = PTYPE_CLIENT;
	}
	if (watchdog[0] == '!') {
		g->watchdog = 0;
	} else {
		g->watchdog = skynet_queryname(ctx, watchdog);
		if (g->watchdog == 0) {
			skynet_error(ctx, "Invalid watchdog %s",watchdog);
			return 1;
		}
	}

	int cap = 16;
	while (cap < max) {
		cap *= 2;
	}
	g->hashmod = cap-1;
	g->max_connection = max;
	g->connection = 0;
	g->client_tag = client_tag;
	g->header_size = header=='S' ? 2 : 4;

	g->hash = malloc(cap * sizeof(struct connection *));
	memset(g->hash, 0, cap * sizeof(struct connection *));

	g->pool  = malloc(max * sizeof(struct connection));
	memset(g->pool, 0, max * sizeof(struct connection));
	int i;
	for (i=0;i<max;i++) {
		g->pool[i].id = -1;
	}

	start_listen(ctx,g,binding);
	skynet_callback(ctx,g,_cb);

	return 0;
}
