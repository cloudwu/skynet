#include "mread.h"
#include "ringbuffer.h"
#include "event.h"

#include <netinet/in.h>
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
#define READBLOCKSIZE 2048
#define RINGBUFFER_DEFAULT 1024 * 1024

#define SOCKET_INVALID 0
#define SOCKET_CLOSED 1
#define SOCKET_SUSPEND 2
#define SOCKET_READ 3
#define SOCKET_POLLIN 4
#define SOCKET_HALFCLOSE 5

#define SOCKET_ALIVE	SOCKET_SUSPEND

#define LISTENSOCKET (void *)((intptr_t)~0)

struct send_buffer {
	struct send_buffer * next;
	int size;
	char * buff;
	char * ptr;
};

struct send_client {
	struct send_buffer * head;
	struct send_buffer * tail;
};

struct socket {
	int fd;
	struct ringbuffer_block * node;
	struct ringbuffer_block * temp;
	struct send_client client;
	int enablewrite;
	int status;
};

struct mread_pool {
	int listen_fd;
	int efd;
	int max_connection;
	int closed;
	int active;
	int skip;
	struct socket * sockets;
	struct socket * free_socket;
	int queue_len;
	int queue_head;
	struct event ev[MAX_EVENT];
	struct ringbuffer * rb;
};

// send begin

static void 
turn_on(struct mread_pool *self, struct socket * s) {
	if (s->status < SOCKET_ALIVE || s->enablewrite) {
		return;
	}
	event_write(self->efd, s->fd, s, true);
	s->enablewrite = 1;
}

static void
turn_off(struct mread_pool *self, struct socket * s) {
	if (s->status < SOCKET_ALIVE || ! s->enablewrite) {
		return;
	}
	s->enablewrite = 0;
	event_write(self->efd, s->fd, s, false);
}

static void
free_buffer(struct send_client * sc) {
	struct send_buffer * sb = sc->head;
	while (sb) {
		struct send_buffer * tmp = sb;
		sb = sb->next;
		free(tmp->ptr);
		free(tmp);
	}
	sc->head = sc->tail = NULL;
}

static void
client_send(struct send_client *c, int fd) {
	while (c->head) {
		struct send_buffer * tmp = c->head;
		for (;;) {
			int sz = write(fd, tmp->buff, tmp->size);
			if (sz < 0) {
				switch(errno) {
				case EINTR:
					continue;
				case EAGAIN:
					return;
				}
				free_buffer(c);
				return;
			}
			if (sz != tmp->size) {
				assert(sz < tmp->size);
				tmp->buff += sz;
				tmp->size -= sz;
				return;
			}
			break;
		}
		c->head = tmp->next;
		free(tmp->ptr);
		free(tmp);
	}
	c->tail = NULL;
}

static void
client_push(struct send_client *c, void * buf, int sz, void * ptr) {
	struct send_buffer * sb = malloc(sizeof(*sb));
	sb->next = NULL;
	sb->buff = buf;
	sb->size = sz;
	sb->ptr = ptr;
	if (c->head) {
		c->tail->next = sb;
		c->tail = sb;
	} else {
		c->head = c->tail = sb;
	}
}

void 
mread_push(struct mread_pool *self, int id, void * buffer, int size, void * ptr) {
	struct socket * s = &self->sockets[id];
	switch(s->status) {
	case SOCKET_INVALID:
	case SOCKET_CLOSED:
	case SOCKET_HALFCLOSE:
		free(ptr);
		return;
	}
	if (s->client.head == NULL) {
		for (;;) {
			int sz = write(s->fd, buffer, size);
			if (sz < 0) {
				switch(errno) {
				case EINTR:
					continue;
				}
				break;
			}
			if (sz == size) {
				// ptr is malloc by 
				//  service-src/service-client.c : 18
				//  uint8_t *tmp = malloc(sz + 4 + 2);
				free(ptr);
				return;
			}
			buffer = (char *)buffer + sz;
			size -= sz;
		}
	} 
	client_push(&s->client,buffer, size, ptr);
	turn_on(self, s);
}

// send end

static struct socket *
_create_sockets(int max) {
	int i;
	struct socket * s = malloc(max * sizeof(struct socket));
	for (i=0;i<max;i++) {
		s[i].fd = i+1;
		s[i].node = NULL;
		s[i].temp = NULL;
		s[i].status = SOCKET_INVALID;
		s[i].enablewrite = 0;
		s[i].client.head = s[i].client.tail = NULL;
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
mread_create(uint32_t addr, int port , int max , int buffer_size) {
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
	my_addr.sin_addr.s_addr = addr;
//	printf("MREAD bind %s:%u\n",inet_ntoa(my_addr.sin_addr),ntohs(my_addr.sin_port));
	if (bind(listen_fd, (struct sockaddr *)&my_addr, sizeof(struct sockaddr)) == -1) {
		close(listen_fd);
		return NULL;
	}
	if (listen(listen_fd, BACKLOG) == -1) {
		close(listen_fd);
		return NULL;
	}

	int efd = event_init(max+1);
	if (efd == -1) {
		close(listen_fd);
		return NULL;
	}

	if (event_add(efd, listen_fd, LISTENSOCKET) == -1) {
		close(listen_fd);
		close(efd);
		return NULL;
	}

	struct mread_pool * self = malloc(sizeof(*self));

	self->listen_fd = listen_fd;
	self->efd = efd;
	self->max_connection = max;
	self->closed = 0;
	self->active = -1;
	self->skip = 0;
	self->sockets = _create_sockets(max);
	self->free_socket = &self->sockets[0];
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

	free_buffer(&s->client);
	free(s);
	mread_close_listen(self);
	close(self->efd);
	_release_rb(self->rb);
	free(self);
}

void
mread_close_listen(struct mread_pool *self) {
	if (self == NULL)
		return;

	if (self->listen_fd >= 0) {
		close(self->listen_fd);
		self->listen_fd = 0;
	}
}

static int
_read_queue(struct mread_pool * self, int timeout) {
	self->queue_head = 0;
	int n = event_wait(self->efd, self->ev, timeout);
	if (n == -1) {
		self->queue_len = 0;
		return -1;
	}
	self->queue_len = n;
	return n;
}

static void
try_close(struct mread_pool * self, struct socket * s) {
	if (s->client.head == NULL) {
		turn_off(self, s);
	}
	if (s->status != SOCKET_HALFCLOSE) {
		return;
	}
	if (s->client.head == NULL) {
		s->status = SOCKET_CLOSED;
		s->node = NULL;
		s->temp = NULL;
		event_del(self->efd, s->fd);
		close(s->fd);
//		printf("MREAD close %d (fd=%d)\n",id,s->fd);
		++self->closed;
	}
}

inline static struct socket *
_read_one(struct mread_pool * self) {
	for (;;) {
		if (self->queue_head >= self->queue_len) {
			return NULL;
		}
		struct socket * ret = self->ev[self->queue_head].s;
		bool writeflag = self->ev[self->queue_head].write;
		bool readflag = self->ev[self->queue_head].read;
		++ self->queue_head;
		if (ret == LISTENSOCKET) {
			return ret;
		}
		if (writeflag && ret->status != SOCKET_CLOSED) {
			client_send(&ret->client, ret->fd);
			try_close(self, ret);
		}
		if (readflag && ret->status != SOCKET_HALFCLOSE &&
			ret->status != SOCKET_CLOSED)
			return ret;
	}
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

static struct socket *
_add_client(struct mread_pool * self, int fd) {
	struct socket * s = _alloc_socket(self);
	if (s == NULL) {
		close(fd);
		return NULL;
	}
	if (event_add(self->efd, fd, s)) {
		close(fd);
		return NULL;
	}
	s->fd = fd;
	s->node = NULL;
	s->status = SOCKET_SUSPEND;
	s->enablewrite = 0;

	return s;
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
	if (self->queue_head >= self->queue_len) {
		if (self->closed > 0 ) {
			return _report_closed(self);
		}
		if (_read_queue(self, timeout) == -1) {
			self->active = -1;
			return -1;
		}
	}
	for (;;) {
		struct socket * s = _read_one(self);
		if (s == NULL) {
			self->active = -1;
			return -1;
		}
		if (s == LISTENSOCKET) {
			struct sockaddr_in remote_addr;
			socklen_t len = sizeof(struct sockaddr_in);
			int client_fd = accept(self->listen_fd , (struct sockaddr *)&remote_addr ,  &len);
			if (client_fd >= 0) {
				_set_nonblocking(client_fd);
//				printf("MREAD connect %s:%u (fd=%d)\n",inet_ntoa(remote_addr.sin_addr),ntohs(remote_addr.sin_port), client_fd);
				struct socket * s = _add_client(self, client_fd);
				if (s) {
					self->active = -1;
					return s - self->sockets;
				}
			}
		} else {
			int index = s - self->sockets;
			assert(index >=0 && index < self->max_connection);
			self->active = index;
			s->status = SOCKET_POLLIN;
			return index;
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
	if (s->status >= SOCKET_ALIVE && s->status != SOCKET_HALFCLOSE) {
		s->status = SOCKET_HALFCLOSE;
		try_close(self, s);
	}
}

static void
force_close_client(struct mread_pool * self, int id) {
	struct socket * s = &self->sockets[id];
	free_buffer(&s->client);
	if (s->status >= SOCKET_ALIVE && s->status != SOCKET_HALFCLOSE) {
		s->status = SOCKET_HALFCLOSE;
		try_close(self, s);
	}
}

static void
_close_active(struct mread_pool * self) {
	int id = self->active;
	struct socket * s = &self->sockets[id];
	ringbuffer_free(self->rb, s->temp);
	ringbuffer_free(self->rb, s->node);
	force_close_client(self, id);
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

static void
skip_all(struct socket *s) {
	char tmp[1024];
	for (;;) {
		int bytes = read(s->fd, tmp, sizeof(tmp));
		if (bytes == 0) {
			free_buffer(&s->client);
		} else if (bytes != sizeof(tmp)) {
			return;
		}
	}
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
	case SOCKET_HALFCLOSE:
		skip_all(s);
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
		force_close_client(self , collect_id);
		if (id == collect_id) {
			return NULL;
		}
		blk = ringbuffer_alloc(rb , rd);
	}

	buffer = (char *)(blk + 1);

	for (;;) {
		int bytes = read(s->fd, buffer, rd);
		if (bytes > 0) {
			ringbuffer_shrink(rb, blk , bytes);
			if (bytes < sz) {
				_link_node(rb, self->active, s , blk);
				s->status = SOCKET_SUSPEND;
				return NULL;
			}
			s->status = SOCKET_READ;
			break;
		}
		if (bytes == 0) {
			ringbuffer_shrink(rb, blk, 0);
			_close_active(self);
			return NULL;
		}
		if (bytes == -1) {
			switch(errno) {
			case EWOULDBLOCK:
				ringbuffer_shrink(rb, blk, 0);
				s->status = SOCKET_SUSPEND;
				return NULL;
			case EINTR:
				continue;
			default:
				ringbuffer_shrink(rb, blk, 0);
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
		force_close_client(self , collect_id);
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
		if (self->free_socket) {
			s->fd = self->free_socket - self->sockets;
		} else {
			s->fd = -1;
		}
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
