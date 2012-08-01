#include "skynet.h"
#include "mread.h"

#include <arpa/inet.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdarg.h>

#define WATCHDOG ".watchdog"

struct connection {
	char * agent;
	int connection_id;
	int uid;
};

struct gate {
	struct mread_pool * pool;
	int id_index;
	int cap;
	int max_connection;
	struct connection ** agent;
	struct connection * map;
};

struct gate *
gate_create(void) {
	struct gate * g = malloc(sizeof(*g));
	g->pool = NULL;
	g->max_connection = 0;
	g->agent = NULL;
	return g;
}

static inline struct connection * 
_id_to_agent(struct gate *g,int uid) {
	return g->agent[uid & (g->cap - 1)];
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
_forward_agent(struct gate * g, int id, char * addr) {
	struct connection * agent = _id_to_agent(g,id);
	if (agent->agent) {
		free(agent->agent);
	}
	agent->agent = strdup(addr);
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
		int connection_id = agent->connection_id;

		mread_close_client(g->pool,connection_id);
		return;
	}
	if (memcmp(command,"forward",i)==0) {
		_parm(tmp, sz, i);
		char * start = tmp;
		char * data = strsep(&start, " ");
		int id = strtol(data , NULL, 10);
		if (start) {
			_forward_agent(g, id, start);
		}
		return;
	}
	skynet_error(ctx, "[gate] Unkown command : %s", command);
}

static void
_report(struct skynet_context * ctx, const char * data, ...) {
	va_list ap;
	va_start(ap, data);
	char tmp[1024];
	int n = vsnprintf(tmp, sizeof(tmp), data, ap);
	va_end(ap);
	if (n>=sizeof(tmp)) {
		n = sizeof(tmp) - 1;
		tmp[n] = '\0';
	}

	skynet_send(ctx, WATCHDOG, strdup(tmp), n);
}

static void
_forward(struct skynet_context * ctx,struct gate *g, int uid, void * data, size_t len) {
	struct connection * agent = _id_to_agent(g,uid);
	char * tmp = malloc(len + 32);
	int n = snprintf(tmp,len+32,"%d data ",uid);
	memcpy(tmp+n,data,len);
	skynet_send(ctx, agent->agent ? agent->agent : WATCHDOG, tmp, len + n);
}

static int
_gen_id(struct gate * g, int connection_id) {
	int uid = ++g->id_index;
	int i;
	for (;;) {
		for (i=0;i<g->cap;i++) {
			int hash = (uid + i) & (g->cap - 1);
			if (g->agent[hash] == NULL) {
				uid = uid + i;
				struct connection * conn = &g->map[connection_id];
				conn->uid = uid;
				g->agent[hash] = conn;
				return uid;
			}
		}
		struct connection ** new_hash = malloc(g->cap * 2 * sizeof(struct connection *));
		memset(new_hash, 0, sizeof(g->cap * 2 * sizeof(struct connection *)));
		for (i=0;i<g->max_connection;i++) {
			struct connection * conn = &g->map[connection_id];
			assert(conn->uid == 0);
			new_hash[conn->uid & (g->cap * 2 -1)] = conn;
		}
		free(g->agent);
		g->agent = new_hash;
	}
}

static void
_remove_id(struct gate *g, int uid) {
	struct connection * conn = _id_to_agent(g,uid);
	assert(conn->uid == uid);
	conn->uid = 0;
	if (conn->agent) {
		free(conn->agent);
		conn->agent = NULL;
	}
}

static void
_cb(struct skynet_context * ctx, void * ud, const char * uid, const void * msg, size_t sz) {
	struct gate *g = ud;
	if (msg) {
		_ctrl(ctx, g , msg , (int)sz);
		return;
	}
	struct mread_pool * m = g->pool;
	int connection_id = mread_poll(m,100);	// timeout : 100ms
	if (connection_id < 0) {
		skynet_command(ctx, "TIMEOUT","1:0");
	} else {
		int id = g->map[connection_id].uid;
		if (id == 0) {
			id = _gen_id(g, connection_id);
			int fd = mread_socket(m , connection_id);
			struct sockaddr_in remote_addr;
			socklen_t len = sizeof(struct sockaddr_in);
			getpeername(fd, (struct sockaddr *)&remote_addr, &len);
			_report(ctx, "%d open %d %s:%u",id,fd,inet_ntoa(remote_addr.sin_addr),ntohs(remote_addr.sin_port));
		}
		uint16_t * plen = mread_pull(m,2);
		if (plen == NULL) {
			if (mread_closed(m)) {
				_remove_id(g,id);
				_report(ctx, "%d close", id);
			}
			goto _break;
		}
		void * data = mread_pull(m, *plen);
		if (data == NULL) {
			if (mread_closed(m)) {
				_remove_id(g,id);
				_report(ctx, "%d close", id);
			}
			goto _break;
		}

		_forward(ctx, g, id, data, *plen);
		mread_yield(m);
_break:
		skynet_command(ctx, "TIMEOUT","0:0");
	}
}

int
gate_init(struct gate *g , struct skynet_context * ctx, char * parm) {
	int port = 0;
	int max = 0;
	int buffer = 0;
	int n = sscanf(parm, "%d %d %d",&port,&max,&buffer);
	if (n!=3) {
		skynet_error(ctx, "Invalid gate parm %s",parm);
		return 1;
	}
	struct mread_pool * pool = mread_create(port, max, buffer);
	if (pool == NULL) {
		skynet_error(ctx, "Create gate %s failed",parm);
		return 1;
	}

	g->pool = pool;
	int cap = 1;
	while (cap < max) {
		cap *= 2;
	}
	g->cap = cap;
	g->max_connection = max;
	g->id_index = 0;

	g->agent = malloc(cap * sizeof(struct connection *));
	memset(g->agent, 0, cap * sizeof(struct connection *));

	g->map  = malloc(max * sizeof(struct connection));
	memset(g->map, 0, max * sizeof(struct connection));
	int i;
	for (i=0;i<max;i++) {
		g->map[i].connection_id = i;
	}

	skynet_callback(ctx,g,_cb);
	skynet_command(ctx,"REG","gate");
	skynet_command(ctx,"TIMEOUT","0:0");

	return 0;
}
