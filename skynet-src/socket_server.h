#ifndef skynet_socket_server_h
#define skynet_socket_server_h

#include <stdint.h>
#include "socket_info.h"
#include "socket_buffer.h"

#define SOCKET_DATA 0
#define SOCKET_CLOSE 1
#define SOCKET_OPEN 2
#define SOCKET_ACCEPT 3
#define SOCKET_ERR 4
#define SOCKET_EXIT 5
#define SOCKET_UDP 6
#define SOCKET_WARNING 7

struct socket_server;

struct socket_message {
	int id;
	uintptr_t opaque;
	int ud;	// for accept, ud is new connection id ; for data, ud is size of data 
	char * data;
};

struct socket_server * socket_server_create(uint64_t time);
void socket_server_release(struct socket_server *);
void socket_server_updatetime(struct socket_server *, uint64_t time);
int socket_server_poll(struct socket_server *, struct socket_message *result, int *more);

void socket_server_exit(struct socket_server *);
void socket_server_close(struct socket_server *, uintptr_t opaque, int id);
void socket_server_shutdown(struct socket_server *, uintptr_t opaque, int id);
void socket_server_start(struct socket_server *, uintptr_t opaque, int id);
void socket_server_pause(struct socket_server *, uintptr_t opaque, int id);

// return -1 when error
int socket_server_send(struct socket_server *, struct socket_sendbuffer *buffer);
int socket_server_send_lowpriority(struct socket_server *, struct socket_sendbuffer *buffer);

// ctrl command below returns id
int socket_server_listen(struct socket_server *, uintptr_t opaque, const char * addr, int port, int backlog);
int socket_server_connect(struct socket_server *, uintptr_t opaque, const char * addr, int port);
int socket_server_bind(struct socket_server *, uintptr_t opaque, int fd);

// for tcp
void socket_server_nodelay(struct socket_server *, int id);

struct socket_udp_address;

// create an udp socket handle, attach opaque with it . udp socket don't need call socket_server_start to recv message
// if port != 0, bind the socket . if addr == NULL, bind ipv4 0.0.0.0 . If you want to use ipv6, addr can be "::" and port 0.
int socket_server_udp(struct socket_server *, uintptr_t opaque, const char * addr, int port);
// set default dest address, return 0 when success
int socket_server_udp_connect(struct socket_server *, int id, const char * addr, int port);
// If the socket_udp_address is NULL, use last call socket_server_udp_connect address instead
// You can also use socket_server_send 
int socket_server_udp_send(struct socket_server *, const struct socket_udp_address *, struct socket_sendbuffer *buffer);
// extract the address of the message, struct socket_message * should be SOCKET_UDP
const struct socket_udp_address * socket_server_udp_address(struct socket_server *, struct socket_message *, int *addrsz);

struct socket_object_interface {
	const void * (*buffer)(const void *);
	size_t (*size)(const void *);
	void (*free)(void *);
};

// if you send package with type SOCKET_BUFFER_OBJECT, use soi.
void socket_server_userobject(struct socket_server *, struct socket_object_interface *soi);

struct socket_info * socket_server_info(struct socket_server *);

#endif
