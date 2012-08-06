#include "skynet.h"

#include <sys/uio.h>
#include <stdlib.h>
#include <stdint.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void
_cb(struct skynet_context * context, void * ud, int session, const char * addr, const void * msg, size_t sz) {
	assert(session == 0);
	assert(sz <= 65535);
	int fd = (int)(intptr_t)ud;

	struct iovec buffer[2];
	uint8_t head[2] = { sz & 0xff , sz >> 8 & 0xff };
	buffer[0].iov_base = head;
	buffer[0].iov_len = 2;
	buffer[1].iov_base = (void *)msg;
	buffer[1].iov_len = sz;

	writev(fd, buffer, 2);
}

int
client_init(void * dummy, struct skynet_context *ctx, const char * args) {
	int fd = strtol(args, NULL, 10);
	skynet_callback(ctx, (void*)(intptr_t)fd, _cb);

	return 0;
}
