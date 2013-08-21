#ifndef skynet_socket_h
#define skynet_socket_h

struct skynet_context;

#define SKYNET_SOCKET_TYPE_DATA 0
#define SKYNET_SOCKET_TYPE_CONNECT 1
#define SKYNET_SOCKET_TYPE_CLOSE 2
#define SKYNET_SOCKET_TYPE_ACCEPT 3
#define SKYNET_SOCKET_TYPE_ERROR 4

struct skynet_socket_message {
	int type;
	int id;
	int ud;
	char * buffer;
};

void skynet_socket_init();
void skynet_socket_exit();
void skynet_socket_free();
void skynet_socket_mainloop();

int skynet_socket_send(struct skynet_context *ctx, int id, void *buffer, int sz);
int skynet_socket_listen(struct skynet_context *ctx, const char *host, int port, int backlog);
int skynet_socket_connect(struct skynet_context *ctx, const char *host, int port);
int skynet_socket_bind(struct skynet_context *ctx, int fd);
void skynet_socket_close(struct skynet_context *ctx, int id);

#endif
