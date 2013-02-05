#include "connection.h"

#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>

/* Test for polling API */
#ifdef __linux__
#define HAVE_EPOLL 1
#endif

#if defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__) || defined (__NetBSD__)
#define HAVE_KQUEUE 1
#endif

#if !defined(HAVE_EPOLL) && !defined(HAVE_KQUEUE)
#error "system does not support epoll or kqueue API"
#endif
/* ! Test for polling API */

#ifdef HAVE_EPOLL
#include <sys/epoll.h>
#elif HAVE_KQUEUE
#include <sys/event.h>
#endif

#define EPOLLQUEUE 32

struct connection_pool {
#ifdef HAVE_EPOLL
	int epoll_fd;
#elif HAVE_KQUEUE
	int kqueue_fd;
#endif
	int queue_len;
	int queue_head;
#ifdef HAVE_EPOLL
	struct epoll_event ev[EPOLLQUEUE];
#elif HAVE_KQUEUE
	struct kevent ev[EPOLLQUEUE];
#endif
};

struct connection_pool * 
connection_newpool(int max) {
#ifdef HAVE_EPOLL
	int epoll_fd = epoll_create(max);
	if (epoll_fd == -1) {
		return NULL;
	}
#elif HAVE_KQUEUE
	int kqueue_fd = kqueue();
	if (kqueue_fd == -1) {
		return NULL;
	}
#endif

	struct connection_pool * pool = malloc(sizeof(*pool));
#ifdef HAVE_EPOLL
	pool->epoll_fd = epoll_fd;
#elif HAVE_KQUEUE
	pool->kqueue_fd = kqueue_fd;
#endif
	pool->queue_len = 0;
	pool->queue_head = 0;

	return pool;
}

void 
connection_deletepool(struct connection_pool * pool) {
#if HAVE_EPOLL
	close(pool->epoll_fd);
#elif HAVE_KQUEUE
	close(pool->kqueue_fd);
#endif
	free(pool);
}

int 
connection_add(struct connection_pool * pool, int fd, void *ud) {
#if HAVE_EPOLL
	struct epoll_event ev;
	ev.events = EPOLLIN;
	ev.data.ptr = ud;

	if (epoll_ctl(pool->epoll_fd, EPOLL_CTL_ADD, fd, &ev) == -1) {
		return 1;
	}
#elif HAVE_KQUEUE
	struct kevent ke;
	EV_SET(&ke, fd, EVFILT_READ, EV_ADD, 0, 0, ud);
	if (kevent(pool->kqueue_fd, &ke, 1, NULL, 0, NULL) == -1) {
		return 1;
	}
#endif
	return 0;
}

void 
connection_del(struct connection_pool * pool, int fd) {
#if HAVE_EPOLL
	struct epoll_event ev;
	epoll_ctl(pool->epoll_fd, EPOLL_CTL_DEL, fd , &ev);
#elif HAVE_KQUEUE
	struct kevent ke;
	EV_SET(&ke, fd, EVFILT_READ, EV_DELETE, 0, 0, NULL);
	kevent(pool->kqueue_fd, &ke, 1, NULL, 0, NULL);
#endif
}

static int
_read_queue(struct connection_pool * pool, int timeout) {
	pool->queue_head = 0;
#if HAVE_EPOLL
	int n = epoll_wait(pool->epoll_fd , pool->ev, EPOLLQUEUE, timeout);
#elif HAVE_KQUEUE
	struct timespec timeoutspec;
	timeoutspec.tv_sec = timeout / 1000;
	timeoutspec.tv_nsec = (timeout % 1000) * 1000000;
	int n = kevent(pool->kqueue_fd, NULL, 0, pool->ev, EPOLLQUEUE, &timeoutspec);
#endif
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
#if HAVE_EPOLL
	return pool->ev[pool->queue_head ++].data.ptr;
#elif HAVE_KQUEUE
	return pool->ev[pool->queue_head ++].udata;
#endif
}

