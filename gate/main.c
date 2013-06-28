#include "skynet.h"
#include "mread.h"

#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdarg.h>

struct connection {
	uint32_t agent;
	uint32_t client;
	int connection_id;
	int uid;
};

struct gate {
	struct mread_pool * pool;
	uint32_t watchdog;
	uint32_t broker;
	int id_index;
	int cap;
	int max_connection;
	int client_tag;
	int header_size;
	struct connection ** agent;
	struct connection * map;
};

struct gate *
gate_create(void) {
	struct gate * g = malloc(sizeof(*g));
	memset(g,0,sizeof(*g));
	return g;
}

static inline struct connection * 
_id_to_agent(struct gate *g,int uid) {
	struct connection * agent = g->agent[uid & (g->cap - 1)];
	if (agent && agent->uid == uid) {
		return agent;
	}
	return NULL;
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
	struct connection * agent = _id_to_agent(g,id);
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
		struct connection * agent = _id_to_agent(g,uid);
		if (agent) {
			int connection_id = agent->connection_id;
			mread_close_client(g->pool,connection_id);
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
		skynet_command(ctx,"TIMEOUT","0");
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
_forward(struct skynet_context * ctx,struct gate *g, int uid, void * data, size_t len) {
	if (g->broker) {
		skynet_send(ctx, 0, g->broker, g->client_tag, 0, data, len);
		return;
	}
	struct connection * agent = _id_to_agent(g,uid);
	if (agent == NULL)
		return;
	if (agent->agent) {
		skynet_send(ctx, agent->client, agent->agent, g->client_tag, 0 , data, len);
	} else if (g->watchdog) {
		char * tmp = malloc(len + 32);
		int n = snprintf(tmp,len+32,"%d data ",uid);
		memcpy(tmp+n,data,len);
		skynet_send(ctx, 0, g->watchdog, PTYPE_TEXT | PTYPE_TAG_DONTCOPY, 0, tmp, len + n);
	}
}

static int
_gen_id(struct gate * g, int connection_id) {
	int uid = ++g->id_index;
	int i;
	for (i=0;i<g->cap;i++) {
		int hash = (uid + i) & (g->cap - 1);
		if (g->agent[hash] == NULL) {
			uid = uid + i;
			struct connection * conn = &g->map[connection_id];
			conn->uid = uid;
			g->agent[hash] = conn;
			g->id_index = uid;
			return uid;
		}
	}
	assert(0);
}

static void
_remove_id(struct gate *g, int uid) {
	struct connection ** pconn = &g->agent[uid & (g->cap - 1)];
	struct connection * conn = *pconn;
	if (conn) {
		assert(conn->uid == uid);
		conn->uid = 0;
		conn->agent = 0;
		*pconn = NULL;
	}
}

static int
_cb(struct skynet_context * ctx, void * ud, int type, int session, uint32_t source, const void * msg, size_t sz) {
	struct gate *g = ud;
	if (type == PTYPE_TEXT) {
		_ctrl(ctx, g , msg , (int)sz);
		return 0;
	} else if (type == PTYPE_CLIENT) {
		if (sz <=4 ) {
			skynet_error(ctx, "Invalid client message from %x",source);
			return 0;
		}
		struct mread_pool * m = g->pool;
		// The first 4 bytes in msg are the id of socket, write following bytes to it
		const uint8_t * data = msg;
		uint32_t uid = data[0] | data[1] << 8 | data[2] << 16 | data[3] << 24;
		struct connection * agent = _id_to_agent(g,uid);
		if (agent) {
			int id = agent->connection_id;
			mread_push(m, id, (void *)(data+4), sz - 4, (void *)data);
			return 1;
		} else {
			skynet_error(ctx, "Invalid client id %d from %x",(int)uid,source);
			return 0;
		}
	}

	assert(type == PTYPE_RESPONSE);
	struct mread_pool * m = g->pool;
	int connection_id = mread_poll(m,100);	// timeout : 100ms
	if (connection_id >= 0) {
		int id = g->map[connection_id].uid;
		if (id == 0) {
			id = _gen_id(g, connection_id);
			int fd = mread_socket(m , connection_id);
			struct sockaddr_in remote_addr;
			socklen_t len = sizeof(struct sockaddr_in);
			getpeername(fd, (struct sockaddr *)&remote_addr, &len);
			_report(g, ctx, "%d open %d %s:%u",id,fd,inet_ntoa(remote_addr.sin_addr),ntohs(remote_addr.sin_port));
		}
		uint8_t * plen = mread_pull(m,g->header_size);
		if (plen == NULL) {
			if (mread_closed(m)) {
				_remove_id(g,id);
				_report(g, ctx, "%d close", id);
			}
			goto _break;
		}
		// big-endian
		int len ;
		if (g->header_size == 2) {
			len = plen[0] << 8 | plen[1];
		} else {
			len = plen[0] << 24 | plen[1] << 16 | plen[2] << 8 | plen[3];
		}

		void * data = mread_pull(m, len);
		if (data == NULL) {
			if (mread_closed(m)) {
				_remove_id(g,id);
				_report(g, ctx, "%d close", id);
			}
			goto _break;
		}

		_forward(ctx, g, id, data, len);
		mread_yield(m);
	}
_break:
	skynet_command(ctx, "TIMEOUT", "0");
	return 0;
}

int
gate_init(struct gate *g , struct skynet_context * ctx, char * parm) {
	int port = 0;
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
		skynet_error(ctx, "Need max conntection");
		return 1;
	}
	if (header != 'S' && header !='L') {
		skynet_error(ctx, "Invalid data header style");
		return 1;
	}
	if (client_tag == 0) {
		client_tag = PTYPE_CLIENT;
	}
	char * portstr = strchr(binding,':');
	uint32_t addr = INADDR_ANY;
	if (portstr == NULL) {
		port = strtol(binding, NULL, 10);
		if (port <= 0) {
			skynet_error(ctx, "Invalid gate address %s",parm);
			return 1;
		}
	} else {
		port = strtol(portstr + 1, NULL, 10);
		if (port <= 0) {
			skynet_error(ctx, "Invalid gate address %s",parm);
			return 1;
		}
		portstr[0] = '\0';
		addr=inet_addr(binding);
	}
	struct mread_pool * pool = mread_create(addr, port, max, buffer);
	if (pool == NULL) {
		skynet_error(ctx, "Create gate %s failed",parm);
		return 1;
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

	g->pool = pool;
	int cap = 1;
	while (cap < max) {
		cap *= 2;
	}
	g->cap = cap;
	g->max_connection = max;
	g->id_index = 0;
	g->client_tag = client_tag;
	g->header_size = header=='S' ? 2 : 4;

	g->agent = malloc(cap * sizeof(struct connection *));
	memset(g->agent, 0, cap * sizeof(struct connection *));

	g->map  = malloc(max * sizeof(struct connection));
	memset(g->map, 0, max * sizeof(struct connection));
	int i;
	for (i=0;i<max;i++) {
		g->map[i].connection_id = i;
	}

	skynet_callback(ctx,g,_cb);

	return 0;
}
