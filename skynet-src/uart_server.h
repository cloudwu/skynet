#ifndef skynet_uart_server_h
#define skynet_uart_server_h

#include "socket_server.h"

#define UART_DATA 0
#define UART_CLOSE 1
#define UART_OPEN 2
#define UART_ERROR 4
#define UART_EXIT 5

int uart_server_open(struct socket_server *, uintptr_t opaque, const char *port);
// fd,115200,0,8,1,'N'
// must use setapi after open a serial
int uart_server_set(struct socket_server *ss, int id,
	int speed, int flow_ctrl, int databits, int stopbits, int parity);

void uart_server_close(struct socket_server *, uintptr_t opaque, int id);

int64_t uart_server_send(struct socket_server *, int id, const void * buffer, int sz);

#endif