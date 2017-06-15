#include "skynet.h"
#include "skynet_socket.h"
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>

#define TIMEOUT "1000"	// 10s

struct response {
	size_t sz;
	void * msg;
};

struct request {
	uint32_t source;
	int session;
};

struct queue {
	int cap;
	int sz;
	int head;
	int tail;
	char * buffer;
};

struct package {
	uint32_t manager;
	int fd;
	int heartbeat;
	int recv;
	int init;
	int closed;

	int header_sz;
	uint8_t header[2];
	int uncomplete_sz;
	struct response uncomplete;

	struct queue request;
	struct queue response;
};

static void
queue_init(struct queue *q, int sz) {
	q->head = 0;
	q->tail = 0;
	q->sz = sz;
	q->cap = 4;
	q->buffer = skynet_malloc(q->cap * q->sz);
}

static void
queue_exit(struct queue *q) {
	skynet_free(q->buffer);
	q->buffer = NULL;
}

static int
queue_empty(struct queue *q) {
	return q->head == q->tail;
}

static int
queue_pop(struct queue *q, void *result) {
	if (q->head == q->tail) {
		return 1;
	}
	memcpy(result, q->buffer + q->head * q->sz, q->sz);
	q->head++;
	if (q->head >= q->cap)
		q->head = 0;
	return 0;
}

static void
queue_push(struct queue *q, const void *value) {
	void * slot = q->buffer + q->tail * q->sz;
	++q->tail;
	if (q->tail >= q->cap)
		q->tail = 0;
	if (q->head == q->tail) {
		// full
		assert(q->sz > 0);
		int cap = q->cap * 2;
		char * tmp = skynet_malloc(cap * q->sz);
		int i;
		int head = q->head;
		for (i=0;i<q->cap;i++) {
			memcpy(tmp + i * q->sz, q->buffer + head * q->sz, q->sz);
			++head;
			if (head >= q->cap) {
				head = 0;
			}
		}
		skynet_free(q->buffer);
		q->head = 0;
		slot = tmp + (q->cap-1) * q->sz;
		q->tail = q->cap;
		q->cap = cap;
		q->buffer = tmp;
	}
	memcpy(slot, value, q->sz);
}

static int
queue_size(struct queue *q) {
	if (q->head > q->tail) {
		return q->tail + q->cap - q->head;
	}
	return q->tail - q->head;
}

static void
service_exit(struct skynet_context *ctx, struct package *P) {
	// report manager
	P->closed = 1;
	while (!queue_empty(&P->request)) {
		struct request req;
		queue_pop(&P->request, &req);
		skynet_send(ctx, 0, req.source, PTYPE_ERROR, req.session, NULL, 0);
	}
	while (!queue_empty(&P->response)) {
		// drop the message
		struct response resp;
		queue_pop(&P->response, &resp);
		skynet_free(resp.msg);
	}
	skynet_send(ctx, 0, P->manager, PTYPE_TEXT, 0, "CLOSED", 6);
	skynet_command(ctx, "EXIT", NULL);
}

static void
report_info(struct skynet_context *ctx, struct package *P, int session, uint32_t source) {
	int uncomplete;
	int uncomplete_sz;
	if (P->header_sz != 0) {
		uncomplete = -1;
		uncomplete_sz = 0;
	} else if (P->uncomplete_sz == 0) {
		uncomplete = 0;
		uncomplete_sz = 0;
	} else {
		uncomplete = P->uncomplete_sz;
		uncomplete_sz = P->uncomplete.sz;
	}
	char tmp[128];
	int n = sprintf(tmp,"req=%d resp=%d uncomplete=%d/%d", queue_size(&P->request), queue_size(&P->response),uncomplete,uncomplete_sz);
	skynet_send(ctx, 0, source, PTYPE_RESPONSE, session, tmp, n);
}

static void
command(struct skynet_context *ctx, struct package *P, int session, uint32_t source, const char *msg, size_t sz) {
	switch (msg[0]) {
	case 'R':
		// request a package
		if (P->closed) {
			skynet_send(ctx, 0, source, PTYPE_ERROR, session, NULL, 0);
			break;
		}
		if (!queue_empty(&P->response)) {
			assert(queue_empty(&P->request));
			struct response resp;
			queue_pop(&P->response, &resp);
			skynet_send(ctx, 0, source, PTYPE_RESPONSE | PTYPE_TAG_DONTCOPY, session, resp.msg, resp.sz);
		} else {
			struct request req;
			req.source = source;
			req.session = session;
			queue_push(&P->request, &req);
		}
		break;
	case 'K':
		// shutdown the connection
		skynet_socket_shutdown(ctx, P->fd);
		break;
	case 'I':
		report_info(ctx, P, session, source);
		break;
	default:
		// invalid command
		skynet_error(ctx, "Invalid command %.*s", (int)sz, msg);
		skynet_send(ctx, 0, source, PTYPE_ERROR, session, NULL, 0);
		break;
	};
}

static void
new_message(struct package *P, const uint8_t *msg, int sz) {
	++P->recv;
	for (;;) {
		if (P->uncomplete_sz >= 0) {
			if (sz >= P->uncomplete_sz) {
				memcpy(P->uncomplete.msg + P->uncomplete.sz - P->uncomplete_sz, msg, P->uncomplete_sz);
				msg += P->uncomplete_sz;
				sz -= P->uncomplete_sz;
				queue_push(&P->response, &P->uncomplete);
				P->uncomplete_sz = -1;
			} else {
				memcpy(P->uncomplete.msg + P->uncomplete.sz - P->uncomplete_sz, msg, sz);
				P->uncomplete_sz -= sz;
				return;
			}
		}

		if (sz <= 0)
			return;

		if (P->header_sz == 0) {
			if (sz == 1) {
				P->header[0] = msg[0];
				P->header_sz = 1;
				return;
			}
			P->header[0] = msg[0];
			P->header[1] = msg[1];
			msg+=2;
			sz-=2;
		} else {
			assert(P->header_sz == 1);
			P->header[1] = msg[0];
			P->header_sz = 0;
			++msg;
			--sz;
		}
		P->uncomplete.sz = P->header[0] * 256 + P->header[1];
		P->uncomplete.msg = skynet_malloc(P->uncomplete.sz);
		P->uncomplete_sz = P->uncomplete.sz;
	}
}

static void
response(struct skynet_context *ctx, struct package *P) {
	while (!queue_empty(&P->request)) {
		if (queue_empty(&P->response)) {
			break;
		}
		struct request req;
		struct response resp;
		queue_pop(&P->request, &req);
		queue_pop(&P->response, &resp);
		skynet_send(ctx, 0, req.source, PTYPE_RESPONSE | PTYPE_TAG_DONTCOPY, req.session, resp.msg, resp.sz);
	}
}

static void
socket_message(struct skynet_context *ctx, struct package *P, const struct skynet_socket_message * smsg) {
	switch (smsg->type) {
	case SKYNET_SOCKET_TYPE_CONNECT:
		if (P->init == 0 && smsg->id == P->fd) {
			skynet_send(ctx, 0, P->manager, PTYPE_TEXT, 0, "SUCC", 4);
			P->init = 1;
		}
		break;
	case SKYNET_SOCKET_TYPE_CLOSE:
	case SKYNET_SOCKET_TYPE_ERROR:
		if (P->init == 0 && smsg->id == P->fd) {
			skynet_send(ctx, 0, P->manager, PTYPE_TEXT, 0, "FAIL", 4);
			P->init = 1;
		}
		if (smsg->id != P->fd) {
			skynet_error(ctx, "Invalid fd (%d), should be (%d)", smsg->id, P->fd);
		} else {
			// todo: log when SKYNET_SOCKET_TYPE_ERROR
			response(ctx, P);
			service_exit(ctx, P);
		}
		break;
	case SKYNET_SOCKET_TYPE_DATA:
		new_message(P, (const uint8_t *)smsg->buffer, smsg->ud);
		skynet_free(smsg->buffer);
		response(ctx, P);
		break;
	case SKYNET_SOCKET_TYPE_WARNING:
		skynet_error(ctx, "Overload on %d", P->fd);
		break;
	default:
		// ignore
		break;
	}
}

static void
heartbeat(struct skynet_context *ctx, struct package *P) {
	if (P->recv == P->heartbeat) {
		if (!P->closed) {
			skynet_socket_shutdown(ctx, P->fd);
			skynet_error(ctx, "timeout %d", P->fd);
		}
	} else {
		P->heartbeat = P->recv = 0;
		skynet_command(ctx, "TIMEOUT", TIMEOUT);
	}
}

static void
send_out(struct skynet_context *ctx, struct package *P, const void *msg, size_t sz) {
	if (sz > 0xffff) {
		skynet_error(ctx, "package too long (%08x)", (uint32_t)sz);
		return;
	}
	uint8_t *p = skynet_malloc(sz + 2);
	p[0] = (sz & 0xff00) >> 8;
	p[1] = sz & 0xff;
	memcpy(p+2, msg, sz);
	skynet_socket_send(ctx, P->fd, p, sz+2);
}

static int
message_handler(struct skynet_context * ctx, void *ud, int type, int session, uint32_t source, const void * msg, size_t sz) {
	struct package *P = ud;
	switch (type) {
	case PTYPE_TEXT:
		command(ctx, P, session, source, msg, sz);
		break;
	case PTYPE_CLIENT:
		send_out(ctx, P, msg, sz);
		break;
	case PTYPE_RESPONSE:
		// It's timer
		heartbeat(ctx, P);
		break;
	case PTYPE_SOCKET:
		socket_message(ctx, P, msg);
		break;
	case PTYPE_ERROR:
		// ignore error
		break;
	default:
		if (session > 0) {
			// unsupport type, raise error
			skynet_send(ctx, 0, source, PTYPE_ERROR, session, NULL, 0);
		}
		break;
	}
	return 0;
}

struct package *
package_create(void) {
	struct package * P = skynet_malloc(sizeof(*P));
	memset(P, 0, sizeof(*P));
	P->heartbeat = -1;
	P->uncomplete_sz = -1;
	queue_init(&P->request, sizeof(struct request));
	queue_init(&P->response, sizeof(struct response));
	return P;
}

void
package_release(struct package *P) {
	queue_exit(&P->request);
	queue_exit(&P->response);
	skynet_free(P);
}

int
package_init(struct package * P, struct skynet_context *ctx, const char * param) {
	int n = sscanf(param ? param : "", "%u %d", &P->manager, &P->fd);
	if (n != 2 || P->manager == 0 || P->fd == 0) {
		skynet_error(ctx, "Invalid param [%s]", param);
		return 1;
	}
	skynet_socket_start(ctx, P->fd);
	skynet_socket_nodelay(ctx, P->fd);
	heartbeat(ctx, P);
	skynet_callback(ctx, P, message_handler);

	return 0;
}
