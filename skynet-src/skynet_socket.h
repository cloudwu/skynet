#ifndef skynet_socket_h
#define skynet_socket_h

#include "socket_info.h"
#include "socket_buffer.h"

struct skynet_context;

#define SKYNET_SOCKET_TYPE_DATA 1
#define SKYNET_SOCKET_TYPE_CONNECT 2
#define SKYNET_SOCKET_TYPE_CLOSE 3
#define SKYNET_SOCKET_TYPE_ACCEPT 4
#define SKYNET_SOCKET_TYPE_ERROR 5
#define SKYNET_SOCKET_TYPE_UDP 6
#define SKYNET_SOCKET_TYPE_WARNING 7

struct skynet_socket_message {
	int type;
	int id;
	int ud;
	char * buffer;
};

void skynet_socket_init();
void skynet_socket_exit();
void skynet_socket_free();
int skynet_socket_poll();
void skynet_socket_updatetime();

int skynet_socket_sendbuffer(struct skynet_context *ctx, struct socket_sendbuffer *buffer);
int skynet_socket_sendbuffer_lowpriority(struct skynet_context *ctx, struct socket_sendbuffer *buffer);
int skynet_socket_listen(struct skynet_context *ctx, const char *host, int port, int backlog);
int skynet_socket_connect(struct skynet_context *ctx, const char *host, int port);
int skynet_socket_bind(struct skynet_context *ctx, int fd);
void skynet_socket_close(struct skynet_context *ctx, int id);
void skynet_socket_shutdown(struct skynet_context *ctx, int id);
void skynet_socket_start(struct skynet_context *ctx, int id);
void skynet_socket_pause(struct skynet_context *ctx, int id);
void skynet_socket_nodelay(struct skynet_context *ctx, int id);

int skynet_socket_udp(struct skynet_context *ctx, const char * addr, int port);
int skynet_socket_udp_connect(struct skynet_context *ctx, int id, const char * addr, int port);
int skynet_socket_udp_sendbuffer(struct skynet_context *ctx, const char * address, struct socket_sendbuffer *buffer);
const char * skynet_socket_udp_address(struct skynet_socket_message *, int *addrsz);

struct socket_info * skynet_socket_info();

// legacy APIs

static inline void sendbuffer_init_(struct socket_sendbuffer *buf, int id, const void *buffer, int sz) {
	buf->id = id;
	buf->buffer = buffer;
	if (sz < 0) {
		buf->type = SOCKET_BUFFER_OBJECT;
	} else {
		buf->type = SOCKET_BUFFER_MEMORY;
	}
	buf->sz = (size_t)sz;
}

static inline int skynet_socket_send(struct skynet_context *ctx, int id, void *buffer, int sz) {
	struct socket_sendbuffer tmp;
	sendbuffer_init_(&tmp, id, buffer, sz);
	return skynet_socket_sendbuffer(ctx, &tmp);
}

static inline int skynet_socket_send_lowpriority(struct skynet_context *ctx, int id, void *buffer, int sz) {
	struct socket_sendbuffer tmp;
	sendbuffer_init_(&tmp, id, buffer, sz);
	return skynet_socket_sendbuffer_lowpriority(ctx, &tmp);
}

static inline int skynet_socket_udp_send(struct skynet_context *ctx, int id, const char * address, const void *buffer, int sz) {
	struct socket_sendbuffer tmp;
	sendbuffer_init_(&tmp, id, buffer, sz);
	return skynet_socket_udp_sendbuffer(ctx, address, &tmp);
}

#endif
