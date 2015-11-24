#ifndef poll_socket_kqueue_h
#define poll_socket_kqueue_h

#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/event.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

static bool 
sp_invalid(int kfd) {
	return kfd == -1;
}

static int
sp_create() {
	return kqueue();
}

static void
sp_release(int kfd) {
	close(kfd);
}

static void 
sp_del(int kfd, int sock) {
	struct kevent ke;
	EV_SET(&ke, sock, EVFILT_READ, EV_DELETE, 0, 0, NULL);
	kevent(kfd, &ke, 1, NULL, 0, NULL);
	EV_SET(&ke, sock, EVFILT_WRITE, EV_DELETE, 0, 0, NULL);
	kevent(kfd, &ke, 1, NULL, 0, NULL);
}

static int 
sp_add(int kfd, int sock, void *ud) {
	struct kevent ke;
	EV_SET(&ke, sock, EVFILT_READ, EV_ADD, 0, 0, ud);
	if (kevent(kfd, &ke, 1, NULL, 0, NULL) == -1) {
		return 1;
	}
	EV_SET(&ke, sock, EVFILT_WRITE, EV_ADD, 0, 0, ud);
	if (kevent(kfd, &ke, 1, NULL, 0, NULL) == -1) {
		EV_SET(&ke, sock, EVFILT_READ, EV_DELETE, 0, 0, NULL);
		kevent(kfd, &ke, 1, NULL, 0, NULL);
		return 1;
	}
	EV_SET(&ke, sock, EVFILT_WRITE, EV_DISABLE, 0, 0, ud);
	if (kevent(kfd, &ke, 1, NULL, 0, NULL) == -1) {
		sp_del(kfd, sock);
		return 1;
	}
	return 0;
}

static void 
sp_write(int kfd, int sock, void *ud, bool enable) {
	struct kevent ke;
	EV_SET(&ke, sock, EVFILT_WRITE, enable ? EV_ENABLE : EV_DISABLE, 0, 0, ud);
	if (kevent(kfd, &ke, 1, NULL, 0, NULL) == -1) {
		// todo: check error
	}
}

static int 
sp_wait(int kfd, struct event *e, int max) {
	struct kevent ev[max];
	int n = kevent(kfd, NULL, 0, ev, max, NULL);

	int i;
	for (i=0;i<n;i++) {
		e[i].s = ev[i].udata;
		unsigned filter = ev[i].filter;
		e[i].write = (filter == EVFILT_WRITE);
		e[i].read = (filter == EVFILT_READ);
	}

	return n;
}

static void
sp_nonblocking(int fd) {
	int flag = fcntl(fd, F_GETFL, 0);
	if ( -1 == flag ) {
		return;
	}

	fcntl(fd, F_SETFL, flag | O_NONBLOCK);
}

#endif
