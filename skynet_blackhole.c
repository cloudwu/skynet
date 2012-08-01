#include "skynet.h"
#include "skynet_blackhole.h"

#include <assert.h>
#include <stdlib.h>
#include <stdio.h>

static void
_log_message(struct skynet_context *ctx, void *ud, const char * uid, const void * msg, size_t sz) {
	const struct blackhole * message = msg;
	assert(sz == sizeof(*message));

	printf("[blackhole] from %d to %s , message_size = %d\n",message->source, message->destination, (int)message->sz);
	free(message->data);
}

int
blackhole_init(void * inst, struct skynet_context *ctx, const char *args) {
	skynet_callback(ctx, inst, _log_message);
	skynet_command(ctx, "REG", "blackhole");
	return 0;
}
