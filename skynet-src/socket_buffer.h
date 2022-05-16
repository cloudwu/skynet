#ifndef socket_buffer_h
#define socket_buffer_h

#include <stdlib.h>

#define SOCKET_BUFFER_MEMORY 0
#define SOCKET_BUFFER_OBJECT 1
#define SOCKET_BUFFER_RAWPOINTER 2

struct socket_sendbuffer {
	int id;
	int type;
	const void *buffer;
	size_t sz;
};

#endif
