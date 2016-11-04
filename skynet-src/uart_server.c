#include "uart_server.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

static int
do_open(const char * port)
{
	int fd = open(port, O_RDWR | O_NOCTTY | O_NDELAY);
	if (fd < 0) {
		return -1;
	}
	if (0 == isatty(STDIN_FILENO)) {
		return -1;
	}
	return fd;
}

int
uart_server_open(struct socket_server *ss, uintptr_t opaque, const char *port)
{
	int fd = do_open(port);
	if (fd < 0) {
		return -1;
	}

	return socket_server_bind(ss, opaque, fd);
}

void uart_server_close(struct socket_server *ss, uintptr_t opaque, int id)
{
	socket_server_close(ss, opaque, id);
}

int64_t uart_server_send(struct socket_server *ss, int id, const void * buffer, int sz)
{
	return socket_server_send(ss, id, buffer, sz);
}
