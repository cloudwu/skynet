#include "skynet.h"
#include "skynet_handle.h"
#include "skynet_multicast.h"
#include "localcast.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static int
_maincb(struct skynet_context * context, void * ud, int type, int session, uint32_t source, const void * msg, size_t sz) {
	const struct localcast *lc = msg;
	size_t s = lc->sz | type << HANDLE_REMOTE_SHIFT;
	struct skynet_multicast_message * mc = skynet_multicast_create(lc->msg, s, source);
	skynet_multicast_cast(context, mc, lc->group, lc->n);
	free((void *)lc->group);
	return 0;
}

int
localcast_init(void * ud, struct skynet_context *ctx, const char * args) {
	skynet_callback(ctx, ud, _maincb);
	skynet_command(ctx, "REG", ".cast");

	return 0;
}



