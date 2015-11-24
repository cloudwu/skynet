#include "skynet.h"

#include "skynet_socket.h"
#include "socket_server.h"
#include "skynet_server.h"
#include "skynet_mq.h"
#include "skynet_harbor.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

static struct socket_server * SOCKET_SERVER = NULL;

void 
skynet_socket_init() {
	SOCKET_SERVER = socket_server_create();
}

void
skynet_socket_exit() {
	socket_server_exit(SOCKET_SERVER);
}

void
skynet_socket_free() {
	socket_server_release(SOCKET_SERVER);
	SOCKET_SERVER = NULL;
}

// mainloop thread
static void
forward_message(int type, bool padding, struct socket_message * result) {
	struct skynet_socket_message *sm;
	size_t sz = sizeof(*sm);
	if (padding) {
		if (result->data) {
			size_t msg_sz = strlen(result->data);
			if (msg_sz > 128) {
				msg_sz = 128;
			}
			sz += msg_sz;
		} else {
			result->data = "";
		}
	}
	sm = (struct skynet_socket_message *)skynet_malloc(sz);
	sm->type = type;
	sm->id = result->id;
	sm->ud = result->ud;
	if (padding) {
		sm->buffer = NULL;
		memcpy(sm+1, result->data, sz - sizeof(*sm));
	} else {
		sm->buffer = result->data;
	}

	struct skynet_message message;
	message.source = 0;
	message.session = 0;
	message.data = sm;
	message.sz = sz | ((size_t)PTYPE_SOCKET << MESSAGE_TYPE_SHIFT);
	
	if (skynet_context_push((uint32_t)result->opaque, &message)) {
		// todo: report somewhere to close socket
		// don't call skynet_socket_close here (It will block mainloop)
		skynet_free(sm->buffer);
		skynet_free(sm);
	}
}

int 
skynet_socket_poll() {
	struct socket_server *ss = SOCKET_SERVER;
	assert(ss);
	struct socket_message result;
	int more = 1;
	int type = socket_server_poll(ss, &result, &more);
	switch (type) {
	case SOCKET_EXIT:
		return 0;
	case SOCKET_DATA:
		forward_message(SKYNET_SOCKET_TYPE_DATA, false, &result);
		break;
	case SOCKET_CLOSE:
		forward_message(SKYNET_SOCKET_TYPE_CLOSE, false, &result);
		break;
	case SOCKET_OPEN:
		forward_message(SKYNET_SOCKET_TYPE_CONNECT, true, &result);
		break;
	case SOCKET_ERROR:
		forward_message(SKYNET_SOCKET_TYPE_ERROR, true, &result);
		break;
	case SOCKET_ACCEPT:
		forward_message(SKYNET_SOCKET_TYPE_ACCEPT, true, &result);
		break;
	case SOCKET_UDP:
		forward_message(SKYNET_SOCKET_TYPE_UDP, false, &result);
		break;
	default:
		skynet_error(NULL, "Unknown socket message type %d.",type);
		return -1;
	}
	if (more) {
		return -1;
	}
	return 1;
}

static int
check_wsz(struct skynet_context *ctx, int id, void *buffer, int64_t wsz) {
	if (wsz < 0) {
		return -1;
	} else if (wsz > 1024 * 1024) {
		struct skynet_socket_message tmp;
		tmp.type = SKYNET_SOCKET_TYPE_WARNING;
		tmp.id = id;
		tmp.ud = (int)(wsz / 1024);
		tmp.buffer = NULL;
		skynet_send(ctx, 0, skynet_context_handle(ctx), PTYPE_SOCKET, 0 , &tmp, sizeof(tmp));
//		skynet_error(ctx, "%d Mb bytes on socket %d need to send out", (int)(wsz / (1024 * 1024)), id);
	}
	return 0;
}

int
skynet_socket_send(struct skynet_context *ctx, int id, void *buffer, int sz) {
	int64_t wsz = socket_server_send(SOCKET_SERVER, id, buffer, sz);
	return check_wsz(ctx, id, buffer, wsz);
}

void
skynet_socket_send_lowpriority(struct skynet_context *ctx, int id, void *buffer, int sz) {
	socket_server_send_lowpriority(SOCKET_SERVER, id, buffer, sz);
}

int 
skynet_socket_listen(struct skynet_context *ctx, const char *host, int port, int backlog) {
	uint32_t source = skynet_context_handle(ctx);
	return socket_server_listen(SOCKET_SERVER, source, host, port, backlog);
}

int 
skynet_socket_connect(struct skynet_context *ctx, const char *host, int port) {
	uint32_t source = skynet_context_handle(ctx);
	return socket_server_connect(SOCKET_SERVER, source, host, port);
}

int 
skynet_socket_bind(struct skynet_context *ctx, int fd) {
	uint32_t source = skynet_context_handle(ctx);
	return socket_server_bind(SOCKET_SERVER, source, fd);
}

void 
skynet_socket_close(struct skynet_context *ctx, int id) {
	uint32_t source = skynet_context_handle(ctx);
	socket_server_close(SOCKET_SERVER, source, id);
}

void 
skynet_socket_shutdown(struct skynet_context *ctx, int id) {
	uint32_t source = skynet_context_handle(ctx);
	socket_server_shutdown(SOCKET_SERVER, source, id);
}

void 
skynet_socket_start(struct skynet_context *ctx, int id) {
	uint32_t source = skynet_context_handle(ctx);
	socket_server_start(SOCKET_SERVER, source, id);
}

void
skynet_socket_nodelay(struct skynet_context *ctx, int id) {
	socket_server_nodelay(SOCKET_SERVER, id);
}

int 
skynet_socket_udp(struct skynet_context *ctx, const char * addr, int port) {
	uint32_t source = skynet_context_handle(ctx);
	return socket_server_udp(SOCKET_SERVER, source, addr, port);
}

int 
skynet_socket_udp_connect(struct skynet_context *ctx, int id, const char * addr, int port) {
	return socket_server_udp_connect(SOCKET_SERVER, id, addr, port);
}

int 
skynet_socket_udp_send(struct skynet_context *ctx, int id, const char * address, const void *buffer, int sz) {
	int64_t wsz = socket_server_udp_send(SOCKET_SERVER, id, (const struct socket_udp_address *)address, buffer, sz);
	return check_wsz(ctx, id, (void *)buffer, wsz);
}

const char *
skynet_socket_udp_address(struct skynet_socket_message *msg, int *addrsz) {
	if (msg->type != SKYNET_SOCKET_TYPE_UDP) {
		return NULL;
	}
	struct socket_message sm;
	sm.id = msg->id;
	sm.opaque = 0;
	sm.ud = msg->ud;
	sm.data = msg->buffer;
	return (const char *)socket_server_udp_address(SOCKET_SERVER, &sm, addrsz);
}
