#include "mread.h"
#include "ringbuffer.h"
#include "map.h"

#include <sys/epoll.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>
#include <fcntl.h>

#define BACKLOG 32
#define READQUEUE 32
#define READBLOCKSIZE 2048
#define RINGBUFFER_DEFAULT 1024 * 1024

#define SOCKET_INVALID 0
#define SOCKET_CLOSED 1
#define SOCKET_SUSPEND 2
#define SOCKET_READ 3
#define SOCKET_POLLIN 4

#define SOCKET_ALIVE	SOCKET_SUSPEND

struct socket {
	int fd;
	struct ringbuffer_block * node;
	struct ringbuffer_block * temp;
	int status;
};

struct mread_pool {
	int listen_fd;
	int epoll_fd;
	int max_connection;
	int closed;
	int active;
	int skip;
	struct socket * sockets;
	struct socket * free_socket;
	struct map * socket_hash;
	int queue_len;
	int queue_head;
	struct epoll_event ev[READQUEUE];
	struct ringbuffer * rb;
};

static struct socket *
_create_sockets(int max) {
	int i;
	struct socket * s = malloc(max * sizeof(struct socket));
	for (i=0;i<max;i++) {
		s[i].fd = i+1;
		s[i].node = NULL;
		s[i].temp = NULL;
		s[i].status = SOCKET_INVALID;
	}
	s[max-1].fd = -1;
	return s;
}

static struct ringbuffer *
_create_rb(int size) {
	size = (size + 3) & ~3;
	if (size < READBLOCKSIZE * 2) {
		size = READBLOCKSIZE * 2;
	}
	struct ringbuffer * rb = ringbuffer_new(size);

	return rb;
}

static void
_release_rb(struct ringbuffer * rb) {
	ringbuffer_delete(rb);
}

static int
_set_nonblocking(int fd)
{
    int flag = fcntl(fd, F_GETFL, 0);
    if ( -1 == flag ) {
        return -1;
    }

    return fcntl(fd, F_SETFL, flag | O_NONBLOCK);
}

struct mread_pool *
mread_create(int port , int max , int buffer_size) {
	int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
	if (listen_fd == -1) {
		return NULL;
	}
	if ( -1 == _set_nonblocking(listen_fd) ) {
		return NULL;
	}

	int reuse = 1;
	setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(int));

	struct sockaddr_in my_addr;
	memset(&my_addr, 0, sizeof(struct sockaddr_in));
	my_addr.sin_family = AF_INET;
	my_addr.sin_port = htons(port);
	my_addr.sin_addr.s_addr = htonl(INADDR_ANY); // INADDR_LOOPBACK
//	printf("MREAD bind %s:%u\n",inet_ntoa(my_addr.sin_addr),ntohs(my_addr.sin_port));
	if (bind(listen_fd, (struct sockaddr *)&my_addr, sizeof(struct sockaddr)) == -1) {
		close(listen_fd);
		return NULL;
	}
	if (listen(listen_fd, BACKLOG) == -1) {
		close(listen_fd);
		return NULL;
	}

	int epoll_fd = epoll_create(max + 1);
	if (epoll_fd == -1) {
		close(listen_fd);
		return NULL;
	}
	struct epoll_event ev;
	ev.events = EPOLLIN;
	ev.data.fd = listen_fd;
	if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, listen_fd, &ev) == -1) {
		close(listen_fd);
		close(epoll_fd);
		return NULL;
	}

	struct mread_pool * self = malloc(sizeof(*self));
	self->listen_fd = listen_fd;
	self->epoll_fd = epoll_fd;
	self->max_connection = max;
	self->closed = 0;
	self->active = -1;
	self->skip = 0;
	self->sockets = _create_sockets(max);
	self->free_socket = &self->sockets[0];
	self->socket_hash = map_new(max * 3 / 2);
	self->queue_len = 0;
	self->queue_head = 0;
	if (buffer_size == 0) {
		self->rb = _create_rb(RINGBUFFER_DEFAULT);
	} else {
		self->rb = _create_rb(buffer_size);
	}

	return self;
}

void
mread_close(struct mread_pool *self) {
	if (self == NULL)
		return;
	int i;
	struct socket * s = self->sockets;
	for (i=0;i<self->max_connection;i++) {
		if (s[i].status >= SOCKET_ALIVE) {
			close(s[i].fd);
		}
	}
	free(s);
	if (self->listen_fd >= 0) {
		close(self->listen_fd);
	}
	close(self->epoll_fd);	
	_release_rb(self->rb);
	map_delete(self->socket_hash);
	free(self);
}

static int
_read_queue(struct mread_pool * self, int timeout) {
	self->queue_head = 0;
	int n = epoll_wait(self->epoll_fd , self->ev, READQUEUE, timeout);
	if (n == -1) {
		self->queue_len = 0;
		return -1;
	}
	self->queue_len = n;
	return n;
}

inline static int
_read_one(struct mread_pool * self) {
	if (self->queue_head >= self->queue_len) {
		return -1;
	}
	return self->ev[self->queue_head ++].data.fd;
}

static struct socket *
_alloc_socket(struct mread_pool * self) {
	if (self->free_socket == NULL) {
		return NULL;
	}
	struct socket * s = self->free_socket;
	int next_free = s->fd;
	if (next_free < 0 ) {
		self->free_socket = NULL;
	} else {
		self->free_socket = &self->sockets[next_free];
	}
	return s;
}

static void
_add_client(struct mread_pool * self, int fd) {
	struct socket * s = _alloc_socket(self);
	if (s == NULL) {
		close(fd);
		return;
	}
	struct epoll_event ev;
	ev.events = EPOLLIN;
	ev.data.fd = fd;
	if (epoll_ctl(self->epoll_fd, EPOLL_CTL_ADD, fd, &ev) == -1) {
		close(fd);
		return;
	}

	s->fd = fd;
	s->node = NULL;
	s->status = SOCKET_SUSPEND;
	int id = s - self->sockets;
	map_insert(self->socket_hash , fd , id);
}

static int
_report_closed(struct mread_pool * self) {
	int i;
	for (i=0;i<self->max_connection;i++) {
		if (self->sockets[i].status == SOCKET_CLOSED) {
			self->active = i;
			return i;
		}
	}
	assert(0);
	return -1;
}

int
mread_poll(struct mread_pool * self , int timeout) {
	self->skip = 0;
	if (self->active >= 0) {
		struct socket * s = &self->sockets[self->active];
		if (s->status == SOCKET_READ) {
			return self->active;
		}
	}
	if (self->closed > 0 ) {
		return _report_closed(self);
	}
	if (self->queue_head >= self->queue_len) {
		if (_read_queue(self, timeout) == -1) {
			self->active = -1;
			return -1;
		}
	}
	for (;;) {
		int fd = _read_one(self);
		if (fd == -1) {
			self->active = -1;
			return -1;
		}
		if (fd == self->listen_fd) {
			struct sockaddr_in remote_addr;
			socklen_t len = sizeof(struct sockaddr_in);
			int client_fd = accept(self->listen_fd , (struct sockaddr *)&remote_addr ,  &len);
			if (client_fd >= 0) {
//				printf("MREAD connect %s:%u (fd=%d)\n",inet_ntoa(remote_addr.sin_addr),ntohs(remote_addr.sin_port), client_fd);
				_add_client(self, client_fd);
			}
		} else {
			int index = map_search(self->socket_hash , fd);
			if (index >= 0) {
				self->active = index;
				struct socket * s = &self->sockets[index];
				s->status = SOCKET_POLLIN;
				return index;
			}
		}
	}
}

int
mread_socket(struct mread_pool * self, int index) {
	return self->sockets[index].fd;
}

static void
_link_node(struct ringbuffer * rb, int id, struct socket * s , struct ringbuffer_block * blk) {
	if (s->node) {
		ringbuffer_link(rb, s->node , blk);	
	} else {
		blk->id = id;
		s->node = blk;
	}
}

void
mread_close_client(struct mread_pool * self, int id) {
	struct socket * s = &self->sockets[id];
	s->status = SOCKET_CLOSED;
	s->node = NULL;
	s->temp = NULL;
	close(s->fd);
//	printf("MREAD close %d (fd=%d)\n",id,s->fd);
	epoll_ctl(self->epoll_fd, EPOLL_CTL_DEL, s->fd , NULL);

	++self->closed;
}

static void
_close_active(struct mread_pool * self) {
	int id = self->active;
	struct socket * s = &self->sockets[id];
	ringbuffer_free(self->rb, s->temp);
	ringbuffer_free(self->rb, s->node);
	mread_close_client(self, id);
}

static char *
_ringbuffer_read(struct mread_pool * self, int *size) {
	struct socket * s = &self->sockets[self->active];
	if (s->node == NULL) {
		*size = 0;
		return NULL;
	}
	int sz = *size;
	void * ret;
	*size = ringbuffer_data(self->rb, s->node, sz , self->skip, &ret);
	return ret;
}

void * 
mread_pull(struct mread_pool * self , int size) {
	if (self->active == -1) {
		return NULL;
	}
	struct socket *s = &self->sockets[self->active];
	int rd_size = size;
	char * buffer = _ringbuffer_read(self, &rd_size);
	if (buffer) {
		self->skip += size;
		return buffer;
	}
	switch (s->status) {
	case SOCKET_READ:
		s->status = SOCKET_SUSPEND;
	case SOCKET_CLOSED:
	case SOCKET_SUSPEND:
		return NULL;
	default:
		assert(s->status == SOCKET_POLLIN);
		break;
	}

	int sz = size - rd_size;
	int rd = READBLOCKSIZE;
	if (rd < sz) {
		rd = sz;
	}

	int id = self->active;
	struct ringbuffer * rb = self->rb;

	struct ringbuffer_block * blk = ringbuffer_alloc(rb , rd);
	while (blk == NULL) {
		int collect_id = ringbuffer_collect(rb);
		mread_close_client(self , collect_id);
		if (id == collect_id) {
			return NULL;
		}
		blk = ringbuffer_alloc(rb , rd);
	}

	buffer = (char *)(blk + 1);

	for (;;) {
		int bytes = recv(s->fd, buffer, rd, MSG_DONTWAIT); 
		if (bytes > 0) {
			ringbuffer_resize(rb, blk , bytes);
			if (bytes < sz) {
				_link_node(rb, self->active, s , blk);
				s->status = SOCKET_SUSPEND;
				return NULL;
			}
			s->status = SOCKET_READ;
			break;
		}
		if (bytes == 0) {
			ringbuffer_resize(rb, blk, 0);
			_close_active(self);
			return NULL;
		}
		if (bytes == -1) {
			switch(errno) {
			case EWOULDBLOCK:
				ringbuffer_resize(rb, blk, 0);
				s->status = SOCKET_SUSPEND;
				return NULL;
			case EINTR:
				continue;
			default:
				ringbuffer_resize(rb, blk, 0);
				_close_active(self);
				return NULL;
			}
		}
	}
	_link_node(rb, self->active , s , blk);
	void * ret;
	int real_rd = ringbuffer_data(rb, s->node , size , self->skip, &ret);
	if (ret) {
		self->skip += size;
		return ret;
	}
	assert(real_rd == size);
	struct ringbuffer_block * temp = ringbuffer_alloc(rb, size);
	while (temp == NULL) {
		int collect_id = ringbuffer_collect(rb);
		mread_close_client(self , collect_id);
		if (id == collect_id) {
			return NULL;
		}
		temp = ringbuffer_alloc(rb , size);
	}
	temp->id = id;
	if (s->temp) {
		ringbuffer_link(rb, temp, s->temp);
	}
	s->temp = temp;
	ret = ringbuffer_copy(rb, s->node, self->skip, temp);
	assert(ret);
	self->skip += size;

	return ret;
}

void 
mread_yield(struct mread_pool * self) {
	if (self->active == -1) {
		return;
	}
	struct socket *s = &self->sockets[self->active];
	ringbuffer_free(self->rb , s->temp);
	s->temp = NULL;
	if (s->status == SOCKET_CLOSED && s->node == NULL) {
		--self->closed;
		s->status = SOCKET_INVALID;
		map_erase(self->socket_hash , s->fd);
		s->fd = self->free_socket - self->sockets;
		self->free_socket = s;
		self->skip = 0;
		self->active = -1;
	} else {
		if (s->node) {
			s->node = ringbuffer_yield(self->rb, s->node, self->skip);
		}
		self->skip = 0;
		if (s->node == NULL) {
			self->active = -1;
		}
	}
}

int 
mread_closed(struct mread_pool * self) {
	if (self->active == -1) {
		return 0;
	}
	struct socket * s = &self->sockets[self->active];
	if (s->status == SOCKET_CLOSED && s->node == NULL) {
		mread_yield(self);
		return 1;
	}
	return 0;
}
