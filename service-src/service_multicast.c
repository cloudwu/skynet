#include "skynet.h"
#include "skynet_handle.h"
#include "skynet_multicast.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

struct skynet_multicast_group *
multicast_create() {
	return skynet_multicast_newgroup();
}

void
multicast_release(struct skynet_multicast_group *g) {
	skynet_multicast_deletegroup(g);
}

static int
_maincb(struct skynet_context * context, void * ud, int type, int session, uint32_t source, const void * msg, size_t sz) {
	struct skynet_multicast_group *g = ud;
	if (type == PTYPE_SYSTEM) {
		char cmd = '\0';
		uint32_t handle = 0;
		sscanf(msg,"%c %x",&cmd,&handle);
		if (handle == 0) {
			skynet_error(context, "Invalid handle %s",msg);
			return 0;
		}
		switch (cmd) {
		case 'E':
			skynet_multicast_entergroup(g, handle);
			break;
		case 'L':
			skynet_multicast_leavegroup(g, handle);
			break;
		case 'C':
			skynet_command(context, "EXIT", NULL);
			break;
		default:
			skynet_error(context, "Invalid command %s",msg);
			break;
		}
		return 0;		
	} else {
		sz |= type << HANDLE_REMOTE_SHIFT;
		struct skynet_multicast_message * mc = skynet_multicast_create(msg, sz, source);
		skynet_multicast_castgroup(context, g, mc);
		return 1;
	}
}

int
multicast_init(struct skynet_multicast_group *g, struct skynet_context *ctx, const char * args) {
	skynet_callback(ctx, g, _maincb);

	return 0;
}



