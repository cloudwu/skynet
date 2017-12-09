#ifndef skynet_socket_server_h
#define skynet_socket_server_h

#include <stdint.h>
// socket_server_poll返回的socket消息类型
#define SOCKET_DATA 0 	// 有数据到来
#define SOCKET_CLOSE 1	// 连接关闭
#define SOCKET_OPEN 2	// 连接建立（主动或者被动，并且已加入到epoll）
#define SOCKET_ACCEPT 3	// 被动连接建立（即accept成功返回已连接套接字）但未加入到epoll
#define SOCKET_ERR 4	// 发生错误
#define SOCKET_EXIT 5	// 退出事件
#define SOCKET_UDP 6
#define SOCKET_WARNING 7

struct socket_server;

struct socket_message {
	int id;
	uintptr_t opaque; // 在skynet中用于保存服务handle
	int ud;	// for accept, ud is new connection id ; for data, ud is size of data 
	char * data;
};

struct socket_server * socket_server_create();// 创建socket_server
void socket_server_release(struct socket_server *);// 销毁socket_server
int socket_server_poll(struct socket_server *, struct socket_message *result, int *more);// 返回事件

void socket_server_exit(struct socket_server *);// 退出socket服务器，导致事件循环退出
void socket_server_close(struct socket_server *, uintptr_t opaque, int id);// 关闭socket
void socket_server_shutdown(struct socket_server *, uintptr_t opaque, int id);
void socket_server_start(struct socket_server *, uintptr_t opaque, int id);// 启动socket，对于监听套接字或者已连接套接字，都要调用该函数，socket才开始工作

// return -1 when error
int socket_server_send(struct socket_server *, int id, const void * buffer, int sz);
int socket_server_send_lowpriority(struct socket_server *, int id, const void * buffer, int sz);

// ctrl command below returns id
int socket_server_listen(struct socket_server *, uintptr_t opaque, const char * addr, int port, int backlog);// socket, bind, listen
int socket_server_connect(struct socket_server *, uintptr_t opaque, const char * addr, int port);// 非阻塞的方式连接
int socket_server_bind(struct socket_server *, uintptr_t opaque, int fd);// 并不对应bind函数，而是将stdin、stdout这类IO加入到epoll管理

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
int socket_server_udp_send(struct socket_server *, int id, const struct socket_udp_address *, const void *buffer, int sz);
// extract the address of the message, struct socket_message * should be SOCKET_UDP
const struct socket_udp_address * socket_server_udp_address(struct socket_server *, struct socket_message *, int *addrsz);

struct socket_object_interface {
	void * (*buffer)(void *);
	int (*size)(void *);
	void (*free)(void *);
};

// if you send package sz == -1, use soi.
void socket_server_userobject(struct socket_server *, struct socket_object_interface *soi);

#endif
