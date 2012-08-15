#include "connection.h"

#include <stdio.h>
#include <sys/epoll.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>

#define EPOLLQUEUE 32

struct connection_pool {
	int epoll_fd;
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
	pool->epoll_fd = epoll_fd;
	pool->queue_len = 0;
	pool->queue_head = 0;

	return pool;
}

void 
connection_deletepool(struct connection_pool * pool) {
	close(pool->epoll_fd);
	free(pool);
}

int 
connection_add(struct connection_pool * pool, int fd, void *ud) {
	struct epoll_event ev;
	ev.events = EPOLLIN;
	ev.data.ptr = ud;

	if (epoll_ctl(pool->epoll_fd, EPOLL_CTL_ADD, fd, &ev) == -1) {
		return 1;
	}
	return 0;
}

void 
connection_del(struct connection_pool * pool, int fd) {
	epoll_ctl(pool->epoll_fd, EPOLL_CTL_DEL, fd , NULL);
	close(fd);
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

void * 
connection_poll(struct connection_pool * pool, int timeout) {
	if (pool->queue_head >= pool->queue_len) {
		if (_read_queue(pool, timeout) <= 0) {
			return NULL;
		}
	}
	return pool->ev[pool->queue_head ++].data.ptr;
}

