#include "skynet.h"
#include "event.h"

#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <assert.h>
#include <unistd.h>
#include <netdb.h>
#include <errno.h>
#include <fcntl.h>

#define MAX_ID 0x7fffffff
#define MAX_CONNECTION 256
#define READ_BUFFER 4000

#define STATUS_INVALID 0
#define STATUS_HALFCLOSE 1
#define STATUS_SUSPEND 2

struct write_buffer {
	struct write_buffer * next;
	char *ptr;
	size_t sz;
	void *buffer;
};

struct socket {
	int fd;
	int id;
	uint32_t source;
	int status;
	int session;
	struct write_buffer * head;
	struct write_buffer * tail;
};

struct socket_pool {
	int fd;
	struct event ev[MAX_EVENT];
	int poll;
	int id;
	int count;
	int cap;
	struct socket * s;
};

static void
reply(struct skynet_context * ctx, uint32_t source, int session, char * cmd, int sz) {
	if (sz < 0) {
		sz = strlen(cmd);
	}
	skynet_send(ctx, 0, source, PTYPE_RESPONSE,  session, cmd, sz);
}

static int
_set_nonblocking(int fd)
{
	int flag = fcntl(fd, F_GETFL, 0);
	if ( -1 == flag ) {
		return -1;
	}

	return fcntl(fd, F_SETFL, flag | O_NONBLOCK);
}

static int
new_socket(struct socket_pool *p, int sock, uint32_t addr) {
	int i;
	if (p->count >= p->cap) {
		goto _error;
	}
	for (i=0;i<p->cap;i++) {
		int id = p->id + i;
		int n = id % p->cap;
		struct socket * s = &p->s[n];
		if (s->status == STATUS_INVALID) {
			if (event_add(p->fd, sock, s)) {
				goto _error;
			}
			s->status = STATUS_SUSPEND;
			_set_nonblocking(sock);
			int keepalive = 1; 
			setsockopt(sock, SOL_SOCKET, SO_KEEPALIVE, (void *)&keepalive , sizeof(keepalive));
			s->fd = sock;
			s->id = id;
			s->source = addr;
			p->count++;
			if (++p->id > MAX_ID) {
				p->id = 1;
			}
			assert(s->head == NULL && s->tail == NULL);
			return id;
		}
	}
_error:
	close(sock);
	return -1;
}

static void
cmd_bind(struct skynet_context * ctx, struct socket_pool *p, int sock, int session, uint32_t source) {
	char ret[10];
	int id = new_socket(p, sock, source);
	if (id<0) {
		reply(ctx, source, session, NULL , 0);
		return;
	}
	if (p->count == 1) {
		skynet_command(ctx, "TIMEOUT", "0");
	}
	int sz = sprintf(ret,"%d",id);
	reply(ctx, source, session, ret, sz);
}

static void
cmd_open(struct skynet_context * ctx, struct socket_pool *p, char * cmd, int session, uint32_t source) {
	int status;
	struct addrinfo ai_hints;
	struct addrinfo *ai_list = NULL;
	struct addrinfo *ai_ptr = NULL;

	char * host = strsep(&cmd, ":");
	if (cmd == NULL) {
		goto _failed;
	}

	memset( &ai_hints, 0, sizeof( ai_hints ) );
	ai_hints.ai_family = AF_UNSPEC;
	ai_hints.ai_socktype = SOCK_STREAM;
	ai_hints.ai_protocol = IPPROTO_TCP;

	status = getaddrinfo( host, cmd, &ai_hints, &ai_list );
	if ( status != 0 ) {
		goto _failed;
	}
	int sock= -1;
	for	( ai_ptr = ai_list;	ai_ptr != NULL;	ai_ptr = ai_ptr->ai_next ) {
		sock = socket( ai_ptr->ai_family, ai_ptr->ai_socktype, ai_ptr->ai_protocol );
		if ( sock < 0 ) {
			continue;
		}
		status = connect( sock,	ai_ptr->ai_addr, ai_ptr->ai_addrlen	);
		if ( status	!= 0 ) {
			close(sock);
			sock = -1;
			continue;
		}
		break;
	}

	freeaddrinfo( ai_list );

	if (sock < 0) {
		goto _failed;
	}

	cmd_bind(ctx, p, sock, session, source);

	return;
_failed:
	reply(ctx, source, session, NULL , 0);
}

static void
force_close(struct socket *s, struct socket_pool *p) {
	struct write_buffer *wb = s->head;
	while (wb) {
		struct write_buffer *tmp = wb;
		wb = wb->next;
		free(tmp->buffer);
		free(tmp);
	}
	s->head = s->tail = NULL;
	s->status = STATUS_INVALID;
	event_del(p->fd, s->fd);
	close(s->fd);
	--p->count;
}

static void
cmd_close(struct skynet_context * ctx, struct socket_pool *p, int id, int session, uint32_t source) {
	struct socket * s = &p->s[id % p->cap];
	if (id != s->id) {
		skynet_error(ctx, "%x close invalid socket %d", source, id);
		reply(ctx, source, session, "invalid", -1);
		return;
	}
	if (source != s->source) {
		skynet_error(ctx, "%x try to close socket %d:%x", source, id, s->source);
		reply(ctx, source, session, "permission", -1);
		return;
	}
	if (s->head == NULL) {
		force_close(s,p);
		reply(ctx, source, session, NULL, 0);
	} else {
		s->status = STATUS_HALFCLOSE;
		s->session = session;
	}
}

static void
_ctrl(struct skynet_context * ctx, struct socket_pool *p, char * command, int id, char * arg, int session, uint32_t source) {
	if (strcmp(command, "open")==0) {
		cmd_open(ctx, p, arg, session, source);
	} else if (strcmp(command, "close")==0) {
		cmd_close(ctx, p, id, session, source);
	} else if (strcmp(command, "bind")==0) {
		cmd_bind(ctx, p, id, session, source);
	} else {
		skynet_error(ctx, "Unknown command %s", command);
		reply(ctx, source, session, NULL, 0);
	}
}

static char *
parser(const char * msg, int sz, char * buffer, int *id) {
	int i;
	for (i=0;i<sz && msg[i] && msg[i]!=' ';i++) {
		if (msg[i] != ' ') {
			buffer[i] = msg[i];
		}
	}
	buffer[i] = '\0';
	if (i+1 < sz) {
		*id = strtol(msg+i+1, NULL, 10);
		memcpy(buffer+i+1,msg+i+1, sz-i-1);
	}
	buffer[sz] = '\0';
	return buffer+i+1;
}

static void
forward(struct skynet_context * context, struct socket *s, struct socket_pool *p) {
	int * buffer = malloc(READ_BUFFER + sizeof(int));
	*buffer = s->id;	// convert endian ?
	int r = 0;
	for (;;) {
		r = read(s->fd, buffer+1, READ_BUFFER);
		if (r == -1) {
			switch(errno) {
			case EWOULDBLOCK:
				free(buffer);
				return;
			case EINTR:
				continue;
			}
			r = 0;
			break;
		}
		break;
	}
	if (r == 0) {
		force_close(s,p);
	}

	if (s->status == STATUS_HALFCLOSE) {
		free(buffer);
	} else {
		skynet_send(context, 0, s->source, PTYPE_CLIENT, 0, buffer, r + 4);
	}
}

static void
sendout(struct socket_pool *p, struct socket *s) {
	while (s->head) {
		struct write_buffer * tmp = s->head;
		for (;;) {
			int sz = write(s->fd, tmp->ptr, tmp->sz);
			if (sz < 0) {
				switch(errno) {
				case EINTR:
					continue;
				case EAGAIN:
					return;
				}
				force_close(s,p);
				return;
			}
			if (sz != tmp->sz) {
				tmp->ptr += sz;
				tmp->sz -= sz;
				return;
			}
			break;
		}
		s->head = tmp->next;
		free(tmp->buffer);
		free(tmp);
	}
	s->tail = NULL;
	event_write(p->fd, s->fd, s, false);
}

static int
try_send(struct skynet_context *ctx, struct socket_pool *p, uint32_t source, const int * msg, size_t sz) {
	if (sz < 4) {
		skynet_error(ctx, "%x invalid message", source);
		return 0;
	}
	sz-=4;
	int id = *msg;
	struct socket * s = &p->s[id % p->cap];
	if (id != s->id) {
		skynet_error(ctx, "%x write to invalid socket %d", source, id);
		return 0;
	}
	if (source != s->source) {
		skynet_error(ctx, "%x try to write socket %d:%x", source, id, s->source);
		return 0;
	}
	if (s->status != STATUS_SUSPEND) {
		skynet_error(ctx, "%x write to closed socket %d", source, id);
		return 0;
	}
	if (s->head) {
		struct write_buffer * buf = malloc(sizeof(*buf));
		buf->ptr = (char *)(msg+1);
		buf->buffer = (void *)msg;
		buf->sz = sz;
		assert(s->tail != NULL);
		assert(s->tail->next == NULL);
		buf->next = s->tail->next;
		s->tail->next = buf;
		s->tail = buf;
		return 1;
	}

	char * ptr = (char *)(msg+1);

	for (;;) {
		int wt = write(s->fd, ptr, sz);
		if (wt < 0) {
			switch(errno) {
			case EINTR:
				continue;
			}
			break;
		}
		if (wt == sz) {
			return 0;
		}
		sz-=wt;
		ptr+=wt;

		break;
	}

	struct write_buffer * buf = malloc(sizeof(*buf));
	buf->next = NULL;
	buf->ptr = ptr;
	buf->sz = sz;
	buf->buffer = (void *)msg;
	s->head = s->tail = buf;

	event_write(p->fd, s->fd, s, true);

	return 1;
}

static int
_cb(struct skynet_context * context, void * ud, int type, int session, uint32_t source, const void * msg, size_t sz) {
	struct socket_pool *p = ud;
	if (type == PTYPE_TEXT) {
		char tmp[sz+1];
		int id=0;
		char * arg = parser(msg, (int)sz, tmp, &id);
		_ctrl(context, p , tmp, id, arg, session, source);
		return 0;
	} else if (type == PTYPE_CLIENT) {
		return try_send(context, p, source, msg, sz);
	}
	if (p->count == 0)
		return 0;
	assert(type == PTYPE_RESPONSE);
	int n = event_wait(p->fd, p->ev, 100); // timeout : 100ms
	int i;
	for (i=0;i<n;i++) {
		struct event *e = &p->ev[i];
		if (e->read) {
			forward(context, e->s, p);
		}
		if (e->write) {
			struct socket *s = e->s;
			sendout(p, s);
			if (s->status == STATUS_HALFCLOSE && s->head == NULL) {
				force_close(s, p);
				reply(context, source, s->session, NULL, 0);
			}
		}
	}
	skynet_command(context, "TIMEOUT", n == MAX_EVENT ? "0" : "1");
	return 0;
}

int
socket_init(struct socket_pool *pool, struct skynet_context *ctx, const char * args) {
	int max = 0;
	sscanf(args, "%d",&max);
	if (max == 0) {
		max = MAX_CONNECTION;
	}
	pool->cap = max;
	int fd = event_init(max);
	if (fd < 0) {
		return 1;
	}
	pool->s = malloc(sizeof(struct socket) * max);
	memset(pool->s, 0, sizeof(struct socket) * max);
	pool->fd = fd;

	skynet_callback(ctx, pool, _cb);
	skynet_command(ctx,"REG",".socket");
	return 0;
}

struct socket_pool *
socket_create(void) {
	struct socket_pool *pool = malloc(sizeof(*pool));
	memset(pool,0,sizeof(*pool));
	pool->id = 1;
	pool->fd = -1;
	return pool;
}

void
socket_release(struct socket_pool *pool) {
	if (pool->fd >= 0) {
		close(pool->fd);
	}
	int i;
	for (i=0;i<pool->cap;i++) {
		if (pool->s[i].status != STATUS_INVALID && pool->s[i].fd >=0) {
			close(pool->s[i].fd);
		}
	}
	free(pool->s);
	free(pool);
}
