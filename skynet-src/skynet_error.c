#include "skynet.h"
#include "skynet_handle.h"
#include "skynet_mq.h"
#include "skynet_server.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static int
error_vasprintf(char **strp, const char *msg, va_list ap) {
	if (strcmp(msg, "%*s") == 0) {
		// for `lerror` in lua-skynet.c
		const int len = va_arg(ap, int);
		const char *str = va_arg(ap, const char*);
		*strp = skynet_strndup(str, len);
		if (*strp == NULL) return -1;
		return len;
	}
	return skynet_vasprintf(strp, msg, ap);
}

void
skynet_error(struct skynet_context * context, const char *msg, ...) {
	static uint32_t logger = 0;
	if (logger == 0) {
		logger = skynet_handle_findname("logger");
	}
	if (logger == 0) {
		return;
	}

	char *data = NULL;

	va_list ap;

	va_start(ap,msg);
	int len = error_vasprintf(&data, msg, ap);
	va_end(ap);

	if (len < 0) {
		skynet_free(data);
		perror("vasprintf error :");
		return;
	}

	struct skynet_message smsg;
	if (context == NULL) {
		smsg.source = 0;
	} else {
		smsg.source = skynet_context_handle(context);
	}
	smsg.session = 0;
	smsg.data = data;
	smsg.sz = len | ((size_t)PTYPE_TEXT << MESSAGE_TYPE_SHIFT);
	skynet_context_push(logger, &smsg);
}
