#include "skynet.h"
#include "skynet_handle.h"
#include "skynet_mq.h"
#include "skynet_server.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define LOG_MESSAGE_SIZE 1024

void 
skynet_error(struct skynet_context * context, const char *msg, ...) {
	static int logger = -1;
	if (logger < 0) {
		logger = skynet_handle_findname("logger");
	}
	if (logger < 0) {
		return;
	}

	char tmp[LOG_MESSAGE_SIZE];

	va_list ap;
	va_start(ap,msg);
	int len = vsnprintf(tmp, LOG_MESSAGE_SIZE, msg, ap);
	va_end(ap);

	if (len >= LOG_MESSAGE_SIZE) {
		len = LOG_MESSAGE_SIZE - 1;
		tmp[len] = '\0';
	}

	struct skynet_message smsg;
	if (context == NULL) {
		smsg.source = 0;
	} else {
		smsg.source = skynet_context_handle(context);
	}
	smsg.session = 0;
	smsg.data = strdup(tmp);
	smsg.sz = len | (PTYPE_TEXT << HANDLE_REMOTE_SHIFT);
	skynet_context_push(logger, &smsg);
}

