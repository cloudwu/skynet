#include "skynet.h"
#include "skynet_handle.h"
#include "skynet_multicast.h"
#include "localcast.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

// localcast本地广播
static int
_maincb(struct skynet_context * context, void * ud, int type, int session, uint32_t source, const void * msg, size_t sz) {
	const struct localcast *lc = msg;
	size_t s = lc->sz | type << HANDLE_REMOTE_SHIFT;

	struct skynet_multicast_message * mc = skynet_multicast_create(lc->msg, s, source);
	skynet_multicast_cast(context, mc, lc->group, lc->n); // skynet_multicast_cast()
	free((void *)lc->group);
	return 0;
}

int
localcast_init(void * ud, struct skynet_context *ctx, const char * args) {
	skynet_callback(ctx, ud, _maincb);
	skynet_command(ctx, "REG", ".cast"); // // 参数以 .xxx 开始的返回本地的服务

	return 0;
}



