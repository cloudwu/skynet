#include "connection.h"

#include <stdio.h>
#include <sys/epoll.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <errno.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#define EPOLLQUEUE 32
#define SEPLEN 8
#define DEFAULT_LINE 64

struct connection {
	int handle;
	int fd;
	short in_epoll;
	short read_complete;
	int readbuffer_cap;
	int read_request;
	int read_sz;
	int yield_sz;
	char sep[SEPLEN];
	char * readbuffer;
};

struct connection_pool {
	int size;
	struct connection * conn;
	int epoll_fd;
	int handle_index;
	int queue_len;
	int queue_head;
	struct epoll_event ev[EPOLLQUEUE];
};

struct connection_pool *
connection_newpool(int max) {
	int epoll_fd = epoll_create(max);
	if (epoll_fd == -1) {
		return NULL;
	}
	struct connection_pool * pool = malloc(sizeof(*pool));
	pool->size = max;
	pool->conn = malloc(max * sizeof(struct connection));
	memset(pool->conn, 0, max * sizeof(struct connection));
	pool->epoll_fd = epoll_fd;
	pool->handle_index =0;
	pool->queue_len = 0;
	pool->queue_head = 0;

	return pool;
}

void
connection_deletepool(struct connection_pool *pool) {
	int i;
	for (i=0;i<pool->size;i++) {
		struct connection * c = &pool->conn[i];
		if (c->handle) {
			close(c->fd);
		}
		if (c->readbuffer) {
			free(c->readbuffer);
		}
	}
	close(pool->epoll_fd);
	free(pool->conn);
	free(pool);
}

static struct connection *
_new_connection(struct connection_pool *pool) {
	int i;
	for (i=1;i<=pool->size;i++) {
		int handle = pool->handle_index + i;
		struct connection *c = &pool->conn[handle % pool->size];
		if (c->handle == 0) {
			c->handle = handle;
			c->in_epoll = 0;
			c->read_complete = 0;
			pool->handle_index = handle;
			return c;
		}
	}
	return NULL;
}

int 
connection_open(struct connection_pool *pool, const char * address) {
	char * sep = strchr(address,':');
	if (sep == NULL)
		return 0;
	int len = sep-address;
	char tmp[len+1];
	memcpy(tmp, address, len);
	tmp[len] = '\0';
	int port = strtol(sep+1, NULL, 10);

	struct sockaddr_in my_addr;
	memset(&my_addr, 0, sizeof(struct sockaddr_in));
	my_addr.sin_family = AF_INET;
	my_addr.sin_port = htons(port);
	my_addr.sin_addr.s_addr=inet_addr(tmp);

	int fd = socket(AF_INET,SOCK_STREAM,0);
	int r = connect(fd,(struct sockaddr *)&my_addr,sizeof(struct sockaddr_in));

	if (r == -1) {
		return 0;
	}

	struct connection * c = _new_connection(pool);
	if (c == NULL) {
		close(fd);
		return 0;
	}
	c->fd = fd;
	return c->handle;
}

static void
_remove_connection(struct connection_pool * pool, struct connection *c) {
	c->handle = 0;
	if (c->in_epoll) {
		epoll_ctl(pool->epoll_fd, EPOLL_CTL_DEL, c->fd , NULL);
	}
	close(c->fd);
}

struct connection *
_get_connection(struct connection_pool *pool, int handle) {
	assert(handle != 0);
	struct connection * c = &pool->conn[handle % pool->size];
	if (c->handle != handle)
		return NULL;
	return c;
}

void 
connection_close(struct connection_pool * pool, int handle) {
	struct connection * c = _get_connection(pool, handle);
	if (c) {
		_remove_connection(pool, c);
	}
}

static void
_add_epoll(struct connection_pool * pool, struct connection *c) {
	if (!c->in_epoll) {
		struct epoll_event ev;
		ev.events = EPOLLIN;
		ev.data.ptr = c;
		if (epoll_ctl(pool->epoll_fd, EPOLL_CTL_ADD, c->fd, &ev) == -1) {
			close(c->fd);
			c->handle = 0;
			return;
		}
		c->in_epoll = 1;
	}
}

static void
_yield(struct connection *c) {
	c->read_complete = 0;
	int len = c->read_sz - c->yield_sz;
	if (len == 0) {
		c->read_sz = 0;
		return;
	}
	assert(len > 0);
	memmove(c->readbuffer, c->readbuffer + c->yield_sz, len);
	c->read_sz = len;
}

void * 
connection_read(struct connection_pool * pool, int handle, size_t sz) {
	struct connection * c = _get_connection(pool, handle);
	if (c == NULL)
		return NULL;
	if (c->read_complete) {
		_yield(c);
	}
	if (c->readbuffer_cap < sz) {
		int cap = DEFAULT_LINE;
		while (cap <= sz) {
			cap *=2;
		}
		c->readbuffer = realloc(c->readbuffer, cap);
		c->readbuffer_cap = cap;
	}

	if (c->read_sz >= sz) {
		c->yield_sz = sz;
		c->read_complete = 1;
		c->read_request = 0;
		return c->readbuffer;
	}

	c->read_request = sz;
	_add_epoll(pool,c);

	return NULL;
}

static const char *
_try_readline(struct connection_pool * pool, struct connection *c) {
	int seplen = strlen(c->sep);
	int i;
	for (i=0;i<= c->read_sz - seplen;i++) {
		if (memcmp(c->readbuffer+i,c->sep,seplen) == 0) {
			c->readbuffer[i] = '\0';
			c->read_request = i;
			c->yield_sz = i + seplen;
			c->read_complete = 1;
			return c->readbuffer;
		}
	}
	return NULL;
}

void * 
connection_readline(struct connection_pool * pool, int handle, const char * sep, size_t *sz) {
	struct connection * c = _get_connection(pool, handle);
	if (c == NULL)
		return NULL;

	if (c->read_complete) {
		_yield(c);
	}
	int i;
	for (i=0;i<SEPLEN;i++) {
		c->sep[i] = sep[i];
		if (sep[i] == '\0')
			break;
	}
	assert(i < SEPLEN);

	const char * line = _try_readline(pool,c);
	if (line) {
		if (sz) {
			*sz = c->read_request;
		}
		c->read_request = 0;
		return c->readbuffer;
	}

	c->read_request = -1;
	_add_epoll(pool,c);

	return NULL;
}

static int
_read_queue(struct connection_pool * pool, int timeout) {
	pool->queue_head = 0;
	int n = epoll_wait(pool->epoll_fd , pool->ev, EPOLLQUEUE, timeout);
	if (n == -1) {
		pool->queue_len = 0;
		return -1;
	}
	pool->queue_len = n;
	return n;
}

inline static struct connection *
_read_one(struct connection_pool * pool) {
	if (pool->queue_head >= pool->queue_len) {
		return NULL;
	}
	return pool->ev[pool->queue_head ++].data.ptr;
}

static int
_read_buffer(struct connection_pool * pool, struct connection *c) {
	int bytes = recv(c->fd, c->readbuffer + c->read_sz, c->readbuffer_cap - c->read_sz, MSG_DONTWAIT);
	if (bytes > 0) {
		c->read_sz += bytes;
	}
	if (bytes == 0) {
		// closed
		_remove_connection(pool, c);
	}

	return bytes;
}

static int
_read_block(struct connection_pool * pool, struct connection *c, size_t * id) {
	assert(c->read_complete == 0);
	int need_read = c->read_request - c->read_sz;
	if (need_read <= 0) {
		c->yield_sz = c->read_request;
		c->read_complete = 1;
		return 1;
	}

	int handle = c->handle;
	int bytes = _read_buffer(pool,c);
	if (bytes > 0) {
		if (c->read_request - c->read_sz <= 0) {
			c->yield_sz = c->read_request;
			c->read_complete = 1;
			return 1;
		} 
	} else if (bytes == 0) {
		if (id) {
			*id = c - pool->conn;
		}
		return -handle;
	}

	return 0;
}

static int
_read_line(struct connection_pool * pool, struct connection *c, size_t * id) {
	assert(c->read_complete == 0);
	if (c->readbuffer_cap == c->read_sz) {
		int cap = DEFAULT_LINE;
		while (cap <= c->read_sz) {
			cap *=2;
		}
		c->readbuffer = realloc(c->readbuffer, cap);
		c->readbuffer_cap = cap;
	}
	int handle = c->handle;
	int bytes = _read_buffer(pool,c);
	if (bytes == 0) {
		if (id) {
			*id = c - pool->conn;
		}
		return -handle;	// close
	}
	const char * line = _try_readline(pool,c);
	if (line)
		return 1;
	return 0;
}

static int
_read_request(struct connection_pool * pool, struct connection *c, size_t * id) {
	if (c->read_request < 0) {
		return _read_line(pool, c, id);
	} else {
		return _read_block(pool, c, id);
	}
}

void * 
connection_poll(struct connection_pool * pool, int timeout, int *phandle, size_t * id) {
	if (pool->queue_head >= pool->queue_len) {
		if (_read_queue(pool, timeout) == -1) {
			return NULL;
		}
	}

	for (;;) {
		struct connection * c = _read_one(pool);
		if (c == NULL) {
			*phandle = 0;
			return NULL;
		}
		if (c->read_request == 0) {
			assert(c->in_epoll);
			epoll_ctl(pool->epoll_fd, EPOLL_CTL_DEL, c->fd , NULL);
			c->in_epoll = 0;
		}
		int result = _read_request(pool, c, id);
		switch (result) {
		case 0:
			// block
			*phandle = 0;
			break;
		case 1:
			*phandle = c->handle;
			if (id) {
				*id = c->read_request;
			}
			c->read_request = 0;
			return c->readbuffer;
		default:
			// nagetive close
			*phandle = -result;
			return NULL;
		}
	}
}

void 
connection_write(struct connection_pool * pool, int handle, const void * buffer, size_t sz) {
	struct connection * c = _get_connection(pool, handle);
	if (c) {
		for (;;) {
			int err = send(c->fd, buffer, sz, 0);
			if (err < 0) {
				switch (errno) {
				case EAGAIN:
				case EINTR:
					continue;
				}
			}
			assert(err == sz);
			return;
		}
	}
}

int 
connection_id(struct connection_pool * pool, int handle) {
	struct connection * c = _get_connection(pool, handle);
	if (c == NULL) {
		return 0;
	}
	return c - pool->conn + 1;
}
