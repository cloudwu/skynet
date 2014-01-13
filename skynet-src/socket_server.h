#ifndef skynet_socket_server_h
#define skynet_socket_server_h

#include <stdint.h>

#define SOCKET_DATA 0
#define SOCKET_CLOSE 1
#define SOCKET_OPEN 2
#define SOCKET_ACCEPT 3
#define SOCKET_ERROR 4
#define SOCKET_EXIT 5

struct socket_server;

struct socket_message {
	int id;
	uintptr_t opaque;
	int ud;	// for accept, ud is listen id ; for data, ud is size of data 
	char * data;
};

struct socket_server * socket_server_create();
void socket_server_release(struct socket_server *);
int socket_server_poll(struct socket_server *, struct socket_message *result, int *more);

void socket_server_exit(struct socket_server *);
void socket_server_close(struct socket_server *, uintptr_t opaque, int id);
void socket_server_start(struct socket_server *, uintptr_t opaque, int id);

// return -1 when error
int64_t socket_server_send(struct socket_server *, int id, const void * buffer, int sz);

// ctrl command below returns id
int socket_server_listen(struct socket_server *, uintptr_t opaque, const char * addr, int port, int backlog);
int socket_server_connect(struct socket_server *, uintptr_t opaque, const char * addr, int port);
int socket_server_bind(struct socket_server *, uintptr_t opaque, int fd);

int socket_server_block_connect(struct socket_server *, uintptr_t opaque, const char * addr, int port);

#endif
