#ifndef socket_info_h
#define socket_info_h

#define SOCKET_INFO_UNKNOWN 0
#define SOCKET_INFO_LISTEN 1
#define SOCKET_INFO_TCP 2
#define SOCKET_INFO_UDP 3
#define SOCKET_INFO_BIND 4
#define SOCKET_INFO_CLOSING 5

#include <stdint.h>

struct socket_info {
	int id;
	int type;
	uint64_t opaque;
	uint64_t read;
	uint64_t write;
	uint64_t rtime;
	uint64_t wtime;
	int64_t wbuffer;
	uint8_t reading;
	uint8_t writing;
	char name[128];
	struct socket_info *next;
};

struct socket_info * socket_info_create(struct socket_info *last);
void socket_info_release(struct socket_info *);

#endif
