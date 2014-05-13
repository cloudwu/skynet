#include "skynet.h"

#include "socket_server.h"
#include "socket_poll.h"

#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <assert.h>
#include <string.h>

#define MAX_INFO 128
// MAX_SOCKET will be 2^MAX_SOCKET_P
#define MAX_SOCKET_P 16
#define MAX_EVENT 64
#define MIN_READ_BUFFER 64
#define SOCKET_TYPE_INVALID 0
#define SOCKET_TYPE_RESERVE 1
#define SOCKET_TYPE_PLISTEN 2
#define SOCKET_TYPE_LISTEN 3
#define SOCKET_TYPE_CONNECTING 4
#define SOCKET_TYPE_CONNECTED 5
#define SOCKET_TYPE_HALFCLOSE 6
#define SOCKET_TYPE_PACCEPT 7
#define SOCKET_TYPE_BIND 8

#define MAX_SOCKET (1<<MAX_SOCKET_P)

#define PRIORITY_HIGH 0
#define PRIORITY_LOW 1

struct write_buffer {
	struct write_buffer * next;
	char *ptr;
	int sz;
	void *buffer;
};

struct wb_list {
	struct write_buffer * head;
	struct write_buffer * tail;
};

struct socket {
	int fd;
	int id;
	int type;
	int size;
	int64_t wb_size;
	uintptr_t opaque;
	struct wb_list high;
	struct wb_list low;
};

struct socket_server {
	int recvctrl_fd;
	int sendctrl_fd;
	int checkctrl;
	poll_fd event_fd;
	int alloc_id;
	int event_n;
	int event_index;
	struct event ev[MAX_EVENT];
	struct socket slot[MAX_SOCKET];
	char buffer[MAX_INFO];
	fd_set rfds;
};

struct request_open {
	int id;
	int port;
	uintptr_t opaque;
	char host[1];
};

struct request_send {
	int id;
	int sz;
	char * buffer;
};

struct request_close {
	int id;
	uintptr_t opaque;
};

struct request_listen {
	int id;
	int fd;
	uintptr_t opaque;
	char host[1];
};

struct request_bind {
	int id;
	int fd;
	uintptr_t opaque;
};

struct request_start {
	int id;
	uintptr_t opaque;
};

struct request_package {
	uint8_t header[8];	// 6 bytes dummy
	union {
		char buffer[256];
		struct request_open open;
		struct request_send send;
		struct request_close close;
		struct request_listen listen;
		struct request_bind bind;
		struct request_start start;
	} u;
	uint8_t dummy[256];
};

union sockaddr_all {
	struct sockaddr s;
	struct sockaddr_in v4;
	struct sockaddr_in6 v6;
};

#define MALLOC skynet_malloc
#define FREE skynet_free

static void
socket_keepalive(int fd) {
	int keepalive = 1;
	setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, (void *)&keepalive , sizeof(keepalive));  
}

static int
reserve_id(struct socket_server *ss) {
	int i;
	for (i=0;i<MAX_SOCKET;i++) {
		int id = __sync_add_and_fetch(&(ss->alloc_id), 1);
		if (id < 0) {
			id = __sync_and_and_fetch(&(ss->alloc_id), 0x7fffffff);
		}
		struct socket *s = &ss->slot[id % MAX_SOCKET];
		if (s->type == SOCKET_TYPE_INVALID) {
			if (__sync_bool_compare_and_swap(&s->type, SOCKET_TYPE_INVALID, SOCKET_TYPE_RESERVE)) {
				return id;
			} else {
				// retry
				--i;
			}
		}
	}
	return -1;
}

static inline void
clear_wb_list(struct wb_list *list) {
	list->head = NULL;
	list->tail = NULL;
}

struct socket_server * 
socket_server_create() {
	int i;
	int fd[2];
	poll_fd efd = sp_create();
	if (sp_invalid(efd)) {
		fprintf(stderr, "socket-server: create event pool failed.\n");
		return NULL;
	}
	if (pipe(fd)) {
		sp_release(efd);
		fprintf(stderr, "socket-server: create socket pair failed.\n");
		return NULL;
	}
	if (sp_add(efd, fd[0], NULL)) {
		// add recvctrl_fd to event poll
		fprintf(stderr, "socket-server: can't add server fd to event pool.\n");
		close(fd[0]);
		close(fd[1]);
		sp_release(efd);
		return NULL;
	}

	struct socket_server *ss = MALLOC(sizeof(*ss));
	ss->event_fd = efd;
	ss->recvctrl_fd = fd[0];
	ss->sendctrl_fd = fd[1];
	ss->checkctrl = 1;

	for (i=0;i<MAX_SOCKET;i++) {
		struct socket *s = &ss->slot[i];
		s->type = SOCKET_TYPE_INVALID;
		clear_wb_list(&s->high);
		clear_wb_list(&s->low);
	}
	ss->alloc_id = 0;
	ss->event_n = 0;
	ss->event_index = 0;
	FD_ZERO(&ss->rfds);
	assert(ss->recvctrl_fd < FD_SETSIZE);

	return ss;
}

static void
free_wb_list(struct wb_list *list) {
	struct write_buffer *wb = list->head;
	while (wb) {
		struct write_buffer *tmp = wb;
		wb = wb->next;
		FREE(tmp->buffer);
		FREE(tmp);
	}
	list->head = NULL;
	list->tail = NULL;
}

static void
force_close(struct socket_server *ss, struct socket *s, struct socket_message *result) {
	result->id = s->id;
	result->ud = 0;
	result->data = NULL;
	result->opaque = s->opaque;
	if (s->type == SOCKET_TYPE_INVALID) {
		return;
	}
	assert(s->type != SOCKET_TYPE_RESERVE);
	free_wb_list(&s->high);
	free_wb_list(&s->low);
	if (s->type != SOCKET_TYPE_PACCEPT && s->type != SOCKET_TYPE_PLISTEN) {
		sp_del(ss->event_fd, s->fd);
	}
	if (s->type != SOCKET_TYPE_BIND) {
		close(s->fd);
	}
	s->type = SOCKET_TYPE_INVALID;
}

void 
socket_server_release(struct socket_server *ss) {
	int i;
	struct socket_message dummy;
	for (i=0;i<MAX_SOCKET;i++) {
		struct socket *s = &ss->slot[i];
		if (s->type != SOCKET_TYPE_RESERVE) {
			force_close(ss, s , &dummy);
		}
	}
	close(ss->sendctrl_fd);
	close(ss->recvctrl_fd);
	sp_release(ss->event_fd);
	FREE(ss);
}

static inline void
check_wb_list(struct wb_list *s) {
	assert(s->head == NULL);
	assert(s->tail == NULL);
}

static struct socket *
new_fd(struct socket_server *ss, int id, int fd, uintptr_t opaque, bool add) {
	struct socket * s = &ss->slot[id % MAX_SOCKET];
	assert(s->type == SOCKET_TYPE_RESERVE);

	if (add) {
		if (sp_add(ss->event_fd, fd, s)) {
			s->type = SOCKET_TYPE_INVALID;
			return NULL;
		}
	}

	s->id = id;
	s->fd = fd;
	s->size = MIN_READ_BUFFER;
	s->opaque = opaque;
	s->wb_size = 0;
	check_wb_list(&s->high);
	check_wb_list(&s->low);
	return s;
}

// return -1 when connecting
static int
open_socket(struct socket_server *ss, struct request_open * request, struct socket_message *result, bool blocking) {
	int id = request->id;
	result->opaque = request->opaque;
	result->id = id;
	result->ud = 0;
	result->data = NULL;
	struct socket *ns;
	int status;
	struct addrinfo ai_hints;
	struct addrinfo *ai_list = NULL;
	struct addrinfo *ai_ptr = NULL;
	char port[16];
	sprintf(port, "%d", request->port);
	memset( &ai_hints, 0, sizeof( ai_hints ) );
	ai_hints.ai_family = AF_UNSPEC;
	ai_hints.ai_socktype = SOCK_STREAM;
	ai_hints.ai_protocol = IPPROTO_TCP;

	status = getaddrinfo( request->host, port, &ai_hints, &ai_list );
	if ( status != 0 ) {
		goto _failed;
	}
	int sock= -1;
	for (ai_ptr = ai_list; ai_ptr != NULL; ai_ptr = ai_ptr->ai_next ) {
		sock = socket( ai_ptr->ai_family, ai_ptr->ai_socktype, ai_ptr->ai_protocol );
		if ( sock < 0 ) {
			continue;
		}
		socket_keepalive(sock);
		if (!blocking) {
			sp_nonblocking(sock);
		}
		status = connect( sock, ai_ptr->ai_addr, ai_ptr->ai_addrlen);
		if ( status != 0 && errno != EINPROGRESS) {
			close(sock);
			sock = -1;
			continue;
		}
		if (blocking) {
			sp_nonblocking(sock);
		}
		break;
	}

	if (sock < 0) {
		goto _failed;
	}

	ns = new_fd(ss, id, sock, request->opaque, true);
	if (ns == NULL) {
		close(sock);
		goto _failed;
	}

	if(status == 0) {
		ns->type = SOCKET_TYPE_CONNECTED;
		struct sockaddr * addr = ai_ptr->ai_addr;
		void * sin_addr = (ai_ptr->ai_family == AF_INET) ? (void*)&((struct sockaddr_in *)addr)->sin_addr : (void*)&((struct sockaddr_in6 *)addr)->sin6_addr;
		if (inet_ntop(ai_ptr->ai_family, sin_addr, ss->buffer, sizeof(ss->buffer))) {
			result->data = ss->buffer;
		}
		freeaddrinfo( ai_list );
		return SOCKET_OPEN;
	} else {
		ns->type = SOCKET_TYPE_CONNECTING;
		sp_write(ss->event_fd, ns->fd, ns, true);
	}

	freeaddrinfo( ai_list );
	return -1;
_failed:
	freeaddrinfo( ai_list );
	ss->slot[id % MAX_SOCKET].type = SOCKET_TYPE_INVALID;
	return SOCKET_ERROR;
}

static int
send_list(struct socket_server *ss, struct socket *s, struct wb_list *list, struct socket_message *result) {
	while (list->head) {
		struct write_buffer * tmp = list->head;
		for (;;) {
			int sz = write(s->fd, tmp->ptr, tmp->sz);
			if (sz < 0) {
				switch(errno) {
				case EINTR:
					continue;
				case EAGAIN:
					return -1;
				}
				force_close(ss,s, result);
				return SOCKET_CLOSE;
			}
			s->wb_size -= sz;
			if (sz != tmp->sz) {
				tmp->ptr += sz;
				tmp->sz -= sz;
				return -1;
			}
			break;
		}
		list->head = tmp->next;
		FREE(tmp->buffer);
		FREE(tmp);
	}
	list->tail = NULL;

	return -1;
}

static inline int
list_uncomplete(struct wb_list *s) {
	struct write_buffer *wb = s->head;
	if (wb == NULL)
		return 0;
	
	return (void *)wb->ptr != wb->buffer;
}

static void
raise_uncomplete(struct socket * s) {
	struct wb_list *low = &s->low;
	struct write_buffer *tmp = low->head;
	low->head = tmp->next;
	if (low->head == NULL) {
		low->tail = NULL;
	}

	// move head of low list (tmp) to the empty high list
	struct wb_list *high = &s->high;
	assert(high->head == NULL);

	tmp->next = NULL;
	high->head = high->tail = tmp;
}

/*
	Each socket has two write buffer list, high priority and low priority.

	1. send high list as far as possible.
	2. If high list is empty, try to send low list.
	3. If low list head is uncomplete (send a part before), move the head of low list to empty high list (call raise_uncomplete) .
	4. If two lists are both empty, turn off the event. (call check_close)
 */
static int
send_buffer(struct socket_server *ss, struct socket *s, struct socket_message *result) {
	assert(!list_uncomplete(&s->low));
	// step 1
	if (send_list(ss,s,&s->high,result) == SOCKET_CLOSE) {
		return SOCKET_CLOSE;
	}
	if (s->high.head == NULL) {
		// step 2
		if (s->low.head != NULL) {
			if (send_list(ss,s,&s->low,result) == SOCKET_CLOSE) {
				return SOCKET_CLOSE;
			}
			// step 3
			if (list_uncomplete(&s->low)) {
				raise_uncomplete(s);
			}
		} else {
			// step 4
			sp_write(ss->event_fd, s->fd, s, false);

			if (s->type == SOCKET_TYPE_HALFCLOSE) {
				force_close(ss, s, result);
				return SOCKET_CLOSE;
			}
		}
	}

	return -1;
}

static int
append_sendbuffer_(struct wb_list *s, struct request_send * request, int n) {
	struct write_buffer * buf = MALLOC(sizeof(*buf));
	buf->ptr = request->buffer+n;
	buf->sz = request->sz - n;
	buf->buffer = request->buffer;
	buf->next = NULL;
	if (s->head == NULL) {
		s->head = s->tail = buf;
	} else {
		assert(s->tail != NULL);
		assert(s->tail->next == NULL);
		s->tail->next = buf;
		s->tail = buf;
	}
	return buf->sz;
}

static inline void
append_sendbuffer(struct socket *s, struct request_send * request, int n) {
	s->wb_size += append_sendbuffer_(&s->high, request, n);
}

static inline void
append_sendbuffer_low(struct socket *s, struct request_send * request) {
	s->wb_size += append_sendbuffer_(&s->low, request, 0);
}

static inline int
send_buffer_empty(struct socket *s) {
	return (s->high.head == NULL && s->low.head == NULL);
}

/*
	When send a package , we can assign the priority : PRIORITY_HIGH or PRIORITY_LOW

	If socket buffer is empty, write to fd directly.
		If write a part, append the rest part to high list. (Even priority is PRIORITY_LOW)
	Else append package to high (PRIORITY_HIGH) or low (PRIORITY_LOW) list.
 */
static int
send_socket(struct socket_server *ss, struct request_send * request, struct socket_message *result, int priority) {
	int id = request->id;
	struct socket * s = &ss->slot[id % MAX_SOCKET];
	if (s->type == SOCKET_TYPE_INVALID || s->id != id 
		|| s->type == SOCKET_TYPE_HALFCLOSE
		|| s->type == SOCKET_TYPE_PACCEPT) {
		FREE(request->buffer);
		return -1;
	}
	assert(s->type != SOCKET_TYPE_PLISTEN && s->type != SOCKET_TYPE_LISTEN);
	if (send_buffer_empty(s)) {
		int n = write(s->fd, request->buffer, request->sz);
		if (n<0) {
			switch(errno) {
			case EINTR:
			case EAGAIN:
				n = 0;
				break;
			default:
				fprintf(stderr, "socket-server: write to %d (fd=%d) error.",id,s->fd);
				force_close(ss,s,result);
				return SOCKET_CLOSE;
			}
		}
		if (n == request->sz) {
			FREE(request->buffer);
			return -1;
		}
		append_sendbuffer(s, request, n);	// add to high priority list, even priority == PRIORITY_LOW
		sp_write(ss->event_fd, s->fd, s, true);
	} else {
		if (priority == PRIORITY_LOW) {
			append_sendbuffer_low(s, request);
		} else {
			append_sendbuffer(s, request, 0);
		}
	}
	return -1;
}

static int
listen_socket(struct socket_server *ss, struct request_listen * request, struct socket_message *result) {
	int id = request->id;
	int listen_fd = request->fd;
	struct socket *s = new_fd(ss, id, listen_fd, request->opaque, false);
	if (s == NULL) {
		goto _failed;
	}
	s->type = SOCKET_TYPE_PLISTEN;
	return -1;
_failed:
	close(listen_fd);
	result->opaque = request->opaque;
	result->id = id;
	result->ud = 0;
	result->data = NULL;
	ss->slot[id % MAX_SOCKET].type = SOCKET_TYPE_INVALID;

	return SOCKET_ERROR;
}

static int
close_socket(struct socket_server *ss, struct request_close *request, struct socket_message *result) {
	int id = request->id;
	struct socket * s = &ss->slot[id % MAX_SOCKET];
	if (s->type == SOCKET_TYPE_INVALID || s->id != id) {
		result->id = id;
		result->opaque = request->opaque;
		result->ud = 0;
		result->data = NULL;
		return SOCKET_CLOSE;
	}
	if (!send_buffer_empty(s)) { 
		int type = send_buffer(ss,s,result);
		if (type != -1)
			return type;
	}
	if (send_buffer_empty(s)) {
		force_close(ss,s,result);
		result->id = id;
		result->opaque = request->opaque;
		return SOCKET_CLOSE;
	}
	s->type = SOCKET_TYPE_HALFCLOSE;

	return -1;
}

static int
bind_socket(struct socket_server *ss, struct request_bind *request, struct socket_message *result) {
	int id = request->id;
	result->id = id;
	result->opaque = request->opaque;
	result->ud = 0;
	struct socket *s = new_fd(ss, id, request->fd, request->opaque, true);
	if (s == NULL) {
		result->data = NULL;
		return SOCKET_ERROR;
	}
	sp_nonblocking(request->fd);
	s->type = SOCKET_TYPE_BIND;
	result->data = "binding";
	return SOCKET_OPEN;
}

static int
start_socket(struct socket_server *ss, struct request_start *request, struct socket_message *result) {
	int id = request->id;
	result->id = id;
	result->opaque = request->opaque;
	result->ud = 0;
	result->data = NULL;
	struct socket *s = &ss->slot[id % MAX_SOCKET];
	if (s->type == SOCKET_TYPE_INVALID || s->id !=id) {
		return SOCKET_ERROR;
	}
	if (s->type == SOCKET_TYPE_PACCEPT || s->type == SOCKET_TYPE_PLISTEN) {
		if (sp_add(ss->event_fd, s->fd, s)) {
			s->type = SOCKET_TYPE_INVALID;
			return SOCKET_ERROR;
		}
		s->type = (s->type == SOCKET_TYPE_PACCEPT) ? SOCKET_TYPE_CONNECTED : SOCKET_TYPE_LISTEN;
		s->opaque = request->opaque;
		result->data = "start";
		return SOCKET_OPEN;
	} else if (s->type == SOCKET_TYPE_CONNECTED) {
		s->opaque = request->opaque;
		result->data = "transfer";
		return SOCKET_OPEN;
	}
	return -1;
}

static void
block_readpipe(int pipefd, void *buffer, int sz) {
	for (;;) {
		int n = read(pipefd, buffer, sz);
		if (n<0) {
			if (errno == EINTR)
				continue;
			fprintf(stderr, "socket-server : read pipe error %s.",strerror(errno));
			return;
		}
		// must atomic read from a pipe
		assert(n == sz);
		return;
	}
}

static int
has_cmd(struct socket_server *ss) {
	struct timeval tv = {0,0};
	int retval;

	FD_SET(ss->recvctrl_fd, &ss->rfds);

	retval = select(ss->recvctrl_fd+1, &ss->rfds, NULL, NULL, &tv);
	if (retval == 1) {
		return 1;
	}
	return 0;
}

// return type
static int
ctrl_cmd(struct socket_server *ss, struct socket_message *result) {
	int fd = ss->recvctrl_fd;
	// the length of message is one byte, so 256+8 buffer size is enough.
	uint8_t buffer[256];
	uint8_t header[2];
	block_readpipe(fd, header, sizeof(header));
	int type = header[0];
	int len = header[1];
	block_readpipe(fd, buffer, len);
	// ctrl command only exist in local fd, so don't worry about endian.
	switch (type) {
	case 'S':
		return start_socket(ss,(struct request_start *)buffer, result);
	case 'B':
		return bind_socket(ss,(struct request_bind *)buffer, result);
	case 'L':
		return listen_socket(ss,(struct request_listen *)buffer, result);
	case 'K':
		return close_socket(ss,(struct request_close *)buffer, result);
	case 'O':
		return open_socket(ss, (struct request_open *)buffer, result, false);
	case 'X':
		result->opaque = 0;
		result->id = 0;
		result->ud = 0;
		result->data = NULL;
		return SOCKET_EXIT;
	case 'D':
		return send_socket(ss, (struct request_send *)buffer, result, PRIORITY_HIGH);
	case 'P':
		return send_socket(ss, (struct request_send *)buffer, result, PRIORITY_LOW);
	default:
		fprintf(stderr, "socket-server: Unknown ctrl %c.\n",type);
		return -1;
	};

	return -1;
}

// return -1 (ignore) when error
static int
forward_message(struct socket_server *ss, struct socket *s, struct socket_message * result) {
	int sz = s->size;
	char * buffer = MALLOC(sz);
	int n = (int)read(s->fd, buffer, sz);
	if (n<0) {
		FREE(buffer);
		switch(errno) {
		case EINTR:
			break;
		case EAGAIN:
			fprintf(stderr, "socket-server: EAGAIN capture.\n");
			break;
		default:
			// close when error
			force_close(ss, s, result);
			return SOCKET_ERROR;
		}
		return -1;
	}
	if (n==0) {
		FREE(buffer);
		force_close(ss, s, result);
		return SOCKET_CLOSE;
	}

	if (s->type == SOCKET_TYPE_HALFCLOSE) {
		// discard recv data
		FREE(buffer);
		return -1;
	}

	if (n == sz) {
		s->size *= 2;
	} else if (sz > MIN_READ_BUFFER && n*2 < sz) {
		s->size /= 2;
	}

	result->opaque = s->opaque;
	result->id = s->id;
	result->ud = n;
	result->data = buffer;
	return SOCKET_DATA;
}

static int
report_connect(struct socket_server *ss, struct socket *s, struct socket_message *result) {
	int error;
	socklen_t len = sizeof(error);  
	int code = getsockopt(s->fd, SOL_SOCKET, SO_ERROR, &error, &len);  
	if (code < 0 || error) {  
		force_close(ss,s, result);
		return SOCKET_ERROR;
	} else {
		s->type = SOCKET_TYPE_CONNECTED;
		result->opaque = s->opaque;
		result->id = s->id;
		result->ud = 0;
		sp_write(ss->event_fd, s->fd, s, false);
		union sockaddr_all u;
		socklen_t slen = sizeof(u);
		if (getpeername(s->fd, &u.s, &slen) == 0) {
			void * sin_addr = (u.s.sa_family == AF_INET) ? (void*)&u.v4.sin_addr : (void *)&u.v6.sin6_addr;
			if (inet_ntop(u.s.sa_family, sin_addr, ss->buffer, sizeof(ss->buffer))) {
				result->data = ss->buffer;
				return SOCKET_OPEN;
			}
		}
		result->data = NULL;
		return SOCKET_OPEN;
	}
}

// return 0 when failed
static int
report_accept(struct socket_server *ss, struct socket *s, struct socket_message *result) {
	union sockaddr_all u;
	socklen_t len = sizeof(u);
	int client_fd = accept(s->fd, &u.s, &len);
	if (client_fd < 0) {
		return 0;
	}
	int id = reserve_id(ss);
	if (id < 0) {
		close(client_fd);
		return 0;
	}
	socket_keepalive(client_fd);
	sp_nonblocking(client_fd);
	struct socket *ns = new_fd(ss, id, client_fd, s->opaque, false);
	if (ns == NULL) {
		close(client_fd);
		return 0;
	}
	ns->type = SOCKET_TYPE_PACCEPT;
	result->opaque = s->opaque;
	result->id = s->id;
	result->ud = id;
	result->data = NULL;

	void * sin_addr = (u.s.sa_family == AF_INET) ? (void*)&u.v4.sin_addr : (void *)&u.v6.sin6_addr;
	if (inet_ntop(u.s.sa_family, sin_addr, ss->buffer, sizeof(ss->buffer))) {
		result->data = ss->buffer;
	}

	return 1;
}

static inline void 
clear_closed_event(struct socket_server *ss, struct socket_message * result, int type) {
	if (type == SOCKET_CLOSE || type == SOCKET_ERROR) {
		int id = result->id;
		int i;
		for (i=ss->event_index; i<ss->event_n; i++) {
			struct event *e = &ss->ev[i];
			struct socket *s = e->s;
			if (s) {
				if (s->type == SOCKET_TYPE_INVALID && s->id == id) {
					e->s = NULL;
				}
			}
		}
	}
}

// return type
int 
socket_server_poll(struct socket_server *ss, struct socket_message * result, int * more) {
	for (;;) {
		if (ss->checkctrl) {
			if (has_cmd(ss)) {
				int type = ctrl_cmd(ss, result);
				if (type != -1) {
					clear_closed_event(ss, result, type);
					return type;
				} else
					continue;
			} else {
				ss->checkctrl = 0;
			}
		}
		if (ss->event_index == ss->event_n) {
			ss->event_n = sp_wait(ss->event_fd, ss->ev, MAX_EVENT);
			ss->checkctrl = 1;
			if (more) {
				*more = 0;
			}
			ss->event_index = 0;
			if (ss->event_n <= 0) {
				ss->event_n = 0;
				return -1;
			}
		}
		struct event *e = &ss->ev[ss->event_index++];
		struct socket *s = e->s;
		if (s == NULL) {
			// dispatch pipe message at beginning
			continue;
		}
		switch (s->type) {
		case SOCKET_TYPE_CONNECTING:
			return report_connect(ss, s, result);
		case SOCKET_TYPE_LISTEN:
			if (report_accept(ss, s, result)) {
				return SOCKET_ACCEPT;
			} 
			break;
		case SOCKET_TYPE_INVALID:
			fprintf(stderr, "socket-server: invalid socket\n");
			break;
		default:
			if (e->write) {
				int type = send_buffer(ss, s, result);
				if (type == -1)
					break;
				clear_closed_event(ss, result, type);
				return type;
			}
			if (e->read) {
				int type = forward_message(ss, s, result);
				if (type == -1)
					break;
				clear_closed_event(ss, result, type);
				return type;
			}
			break;
		}
	}
}

static void
send_request(struct socket_server *ss, struct request_package *request, char type, int len) {
	request->header[6] = (uint8_t)type;
	request->header[7] = (uint8_t)len;
	for (;;) {
		int n = write(ss->sendctrl_fd, &request->header[6], len+2);
		if (n<0) {
			if (errno != EINTR) {
				fprintf(stderr, "socket-server : send ctrl command error %s.\n", strerror(errno));
			}
			continue;
		}
		assert(n == len+2);
		return;
	}
}

static int
open_request(struct socket_server *ss, struct request_package *req, uintptr_t opaque, const char *addr, int port) {
	int len = strlen(addr);
	if (len + sizeof(req->u.open) > 256) {
		fprintf(stderr, "socket-server : Invalid addr %s.\n",addr);
		return 0;
	}
	int id = reserve_id(ss);
	req->u.open.opaque = opaque;
	req->u.open.id = id;
	req->u.open.port = port;
	memcpy(req->u.open.host, addr, len);
	req->u.open.host[len] = '\0';

	return len;
}

int 
socket_server_connect(struct socket_server *ss, uintptr_t opaque, const char * addr, int port) {
	struct request_package request;
	int len = open_request(ss, &request, opaque, addr, port);
	send_request(ss, &request, 'O', sizeof(request.u.open) + len);
	return request.u.open.id;
}

int 
socket_server_block_connect(struct socket_server *ss, uintptr_t opaque, const char * addr, int port) {
	struct request_package request;
	struct socket_message result;
	open_request(ss, &request, opaque, addr, port);
	int ret = open_socket(ss, &request.u.open, &result, true);
	if (ret == SOCKET_OPEN) {
		return result.id;
	} else {
		return -1;
	}
}

// return -1 when error
int64_t 
socket_server_send(struct socket_server *ss, int id, const void * buffer, int sz) {
	struct socket * s = &ss->slot[id % MAX_SOCKET];
	if (s->id != id || s->type == SOCKET_TYPE_INVALID) {
		return -1;
	}
	assert(s->type != SOCKET_TYPE_RESERVE);

	struct request_package request;
	request.u.send.id = id;
	request.u.send.sz = sz;
	request.u.send.buffer = (char *)buffer;

	send_request(ss, &request, 'D', sizeof(request.u.send));
	return s->wb_size;
}

void 
socket_server_send_lowpriority(struct socket_server *ss, int id, const void * buffer, int sz) {
	struct socket * s = &ss->slot[id % MAX_SOCKET];
	if (s->id != id || s->type == SOCKET_TYPE_INVALID) {
		return;
	}
	assert(s->type != SOCKET_TYPE_RESERVE);

	struct request_package request;
	request.u.send.id = id;
	request.u.send.sz = sz;
	request.u.send.buffer = (char *)buffer;

	send_request(ss, &request, 'P', sizeof(request.u.send));
}

void
socket_server_exit(struct socket_server *ss) {
	struct request_package request;
	send_request(ss, &request, 'X', 0);
}

void
socket_server_close(struct socket_server *ss, uintptr_t opaque, int id) {
	struct request_package request;
	request.u.close.id = id;
	request.u.close.opaque = opaque;
	send_request(ss, &request, 'K', sizeof(request.u.close));
}

static int
do_listen(const char * host, int port, int backlog) {
	// only support ipv4
	// todo: support ipv6 by getaddrinfo
	uint32_t addr = INADDR_ANY;
	if (host[0]) {
		addr=inet_addr(host);
	}
	int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
	if (listen_fd < 0) {
		return -1;
	}
	int reuse = 1;
	if (setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, (void *)&reuse, sizeof(int))==-1) {
		goto _failed;
	}

	struct sockaddr_in my_addr;
	memset(&my_addr, 0, sizeof(struct sockaddr_in));
	my_addr.sin_family = AF_INET;
	my_addr.sin_port = htons(port);
	my_addr.sin_addr.s_addr = addr;
	if (bind(listen_fd, (struct sockaddr *)&my_addr, sizeof(struct sockaddr)) == -1) {
		goto _failed;
	}
	if (listen(listen_fd, backlog) == -1) {
		goto _failed;
	}
	return listen_fd;
_failed:
	close(listen_fd);
	return -1;
}

int 
socket_server_listen(struct socket_server *ss, uintptr_t opaque, const char * addr, int port, int backlog) {
	int fd = do_listen(addr, port, backlog);
	if (fd < 0) {
		return -1;
	}
	struct request_package request;
	int id = reserve_id(ss);
	request.u.listen.opaque = opaque;
	request.u.listen.id = id;
	request.u.listen.fd = fd;
	send_request(ss, &request, 'L', sizeof(request.u.listen));
	return id;
}

int
socket_server_bind(struct socket_server *ss, uintptr_t opaque, int fd) {
	struct request_package request;
	int id = reserve_id(ss);
	request.u.bind.opaque = opaque;
	request.u.bind.id = id;
	request.u.bind.fd = fd;
	send_request(ss, &request, 'B', sizeof(request.u.bind));
	return id;
}

void 
socket_server_start(struct socket_server *ss, uintptr_t opaque, int id) {
	struct request_package request;
	request.u.start.id = id;
	request.u.start.opaque = opaque;
	send_request(ss, &request, 'S', sizeof(request.u.start));
}


