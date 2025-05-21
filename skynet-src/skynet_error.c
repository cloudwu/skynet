#include "skynet.h"
#include "skynet_handle.h"
#include "skynet_imp.h"
#include "skynet_mq.h"
#include "skynet_server.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#define LOG_MESSAGE_SIZE 256

static int
log_try_vasprintf(char **strp, const char *fmt, va_list ap) {
	if (strcmp(fmt, "%*s") == 0) {
		// for `lerror` in lua-skynet.c
		const int len = va_arg(ap, int);
		const char *tmp = va_arg(ap, const char*);
		*strp = skynet_strndup(tmp, len);
		return *strp != NULL ? len : -1;
	}

	char tmp[LOG_MESSAGE_SIZE];
	int len = vsnprintf(tmp, LOG_MESSAGE_SIZE, fmt, ap);
	if (len >= 0 && len < LOG_MESSAGE_SIZE) {
		*strp = skynet_strndup(tmp, len);
		if (*strp == NULL) return -1;
	}
	return len;
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

	va_start(ap, msg);
	int len = log_try_vasprintf(&data, msg, ap);
	va_end(ap);
	if (len < 0) {
		perror("vasprintf error :");
		return;
	}

	if (data == NULL) { // unlikely
		data = skynet_malloc(len + 1);
		va_start(ap, msg);
		len = vsnprintf(data, len + 1, msg, ap);
		va_end(ap);
		if (len < 0) {
			skynet_free(data);
			perror("vsnprintf error :");
			return;
		}
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
