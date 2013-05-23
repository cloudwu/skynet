#include "skynet.h"

#include <sys/uio.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>
#include <stdint.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>

struct send_buffer {
	struct send_buffer * next;
	int size;
	char buf[2];
};

struct client {
	int fd;
	struct send_buffer * head;
	struct send_buffer * tail;
};

static struct send_buffer *
sb_alloc(size_t sz, const char * msg, int alreadysend) {
	struct send_buffer * sb = malloc(sizeof(struct send_buffer) + sz - alreadysend);
	if (alreadysend <= 0) {
		sb->buf[0] = sz >> 8 & 0xff;
		sb->buf[1] = sz & 0xff;
		memcpy(sb->buf+2, msg, sz);
		sb->size = sz + 2;
	} else if (alreadysend == 1) {
		sb->buf[0] = sz & 0xff;
		memcpy(sb->buf+1, msg, sz);
		sb->size = sz + 1;
	} else {
		sb->size = sz + 2 - alreadysend;
		memcpy(sb->buf, msg, sb->size);
	}
	sb->next = NULL;
	return sb;
}

static void
sb_free_all(struct send_buffer * sb) {
	while (sb) {
		struct send_buffer * tmp = sb;
		sb = sb->next;
		free(tmp);
	}
}

static void
send_all(struct client *c) {
	while (c->head) {
		struct send_buffer * tmp = c->head;
		for (;;) {
			int sz = write(c->fd, tmp->buf, tmp->size);
			if (sz < 0) {
				switch(errno) {
				case EAGAIN:
				case EINTR:
					continue;
				}
				return;
			}
			if (sz != tmp->size) {
				assert(sz < tmp->size);
				memmove(tmp->buf, tmp->buf + sz, tmp->size - sz);
				tmp->size -= sz;
				return;
			}
			break;
		}
		c->head = tmp->next;
		free(tmp);
	}
	c->tail = NULL;
}

static int
_cb(struct skynet_context * context, void * ud, int type, int session, uint32_t source, const void * msg, size_t sz) {
	assert(sz <= 65535);
	struct client * c = ud;
	send_all(c);
	if (c->head) {
		struct send_buffer * tmp = sb_alloc(sz, msg, 0);
		c->tail->next = tmp;
		c->tail = tmp;
		return 0;
	}

	int fd = c->fd;

	struct iovec buffer[2];
	// send big-endian header
	uint8_t head[2] = { sz >> 8 & 0xff , sz & 0xff };
	buffer[0].iov_base = head;
	buffer[0].iov_len = 2;
	buffer[1].iov_base = (void *)msg;
	buffer[1].iov_len = sz;

	for (;;) {
		int err = writev(fd, buffer, 2);
		if (err < 0) {
			switch (errno) {
			case EAGAIN:
			case EINTR:
				continue;
			}
		}
		if ( err != sz + 2) {
			c->head = c->tail = sb_alloc(sz, msg, err);
		}
		return 0;
	}
}

int
client_init(struct client *c, struct skynet_context *ctx, const char * args) {
	int fd = strtol(args, NULL, 10);
	c->fd = fd;
	skynet_callback(ctx, c, _cb);

	return 0;
}

struct client *
client_create(void) {
	struct client *c = malloc(sizeof(*c));
	memset(c,0,sizeof(*c));
	return c;
}

void
client_release(struct client *c) {
	sb_free_all(c->head);
	free(c);
}
