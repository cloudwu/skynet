#include "skynet.h"

#include <stdint.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct client {
	int gate;
	uint8_t id[4];
};

static int
_cb(struct skynet_context * context, void * ud, int type, int session, uint32_t source, const void * msg, size_t sz) {
	assert(sz <= 65535);
	struct client * c = ud;
	// tmp will be free by gate.
	// see gate/mread.c : mread_push()
	uint8_t *tmp = malloc(sz + 4 + 2);
	memcpy(tmp, c->id, 4);
	tmp[4] = (sz >> 8) & 0xff;
	tmp[5] = sz & 0xff;
	memcpy(tmp+6, msg, sz);

	skynet_send(context, source, c->gate, PTYPE_CLIENT | PTYPE_TAG_DONTCOPY, 0, tmp, sz+6);

	return 0;
}

int
client_init(struct client *c, struct skynet_context *ctx, const char * args) {
	int fd = 0, gate = 0, id = 0;
	sscanf(args, "%d %d %d",&fd,&gate,&id);
	if (gate == 0) {
		skynet_error(ctx, "Invalid init client %s",args);
		return 1;
	}
	c->gate = gate;
	c->id[0] = id & 0xff;
	c->id[1] = (id >> 8) & 0xff;
	c->id[2] = (id >> 16) & 0xff;
	c->id[3] = (id >> 24) & 0xff;
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
	free(c);
}
