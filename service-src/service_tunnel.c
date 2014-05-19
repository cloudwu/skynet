#include "skynet.h"
#include <stdio.h>

// 设置转发地址 即设置 forward的目的地址
static int
_cb(struct skynet_context * context, void * ud, int type, int session, uint32_t source, const void * msg, size_t sz) {
	uint32_t dest = (uint32_t)(uintptr_t)ud;
	skynet_forward(context, dest);
	return 0;
}

// tunnel
int
tunnel_init(void * dummy, struct skynet_context *ctx, const char * args) {
	uint32_t dest = skynet_queryname(ctx, args);
	if (dest == 0) {
		skynet_error(ctx, "Can't create tunnel to %s",args);
		return 1;
	}

	skynet_callback(ctx, (void*)(intptr_t)dest, _cb);

	return 0;
}
