#include "skynet.h"
#include "skynet_harbor.h"
#include "skynet_server.h"

#include <string.h>
#include <stdio.h>
#include <assert.h>

static struct skynet_context * REMOTE = 0;
static unsigned int HARBOR = 0;

void 
skynet_harbor_send(struct remote_message *rmsg, uint32_t source, int session) {
	int type = rmsg->sz >> HANDLE_REMOTE_SHIFT;
	rmsg->sz &= HANDLE_MASK;
	assert(type != PTYPE_SYSTEM && type != PTYPE_HARBOR);
	skynet_context_send(REMOTE, rmsg, sizeof(*rmsg) , source, type , session);
}

void 
skynet_harbor_register(struct remote_name *rname) {
	int i;
	int number = 1;
	for (i=0;i<GLOBALNAME_LENGTH;i++) {
		char c = rname->name[i];
		if (!(c >= '0' && c <='9')) {
			number = 0;
			break;
		}
	}
	assert(number == 0);
	skynet_context_send(REMOTE, rname, sizeof(*rname), 0, PTYPE_SYSTEM , 0);
}

int 
skynet_harbor_message_isremote(uint32_t handle) {
	assert(HARBOR != 0);
	int h = (handle & ~HANDLE_MASK);
	return h != HARBOR && h !=0;
}

void
skynet_harbor_init(int harbor) {
	HARBOR = (unsigned int)harbor << HANDLE_REMOTE_SHIFT;
}

void
skynet_harbor_start(void *ctx) {
	REMOTE = ctx;
}
