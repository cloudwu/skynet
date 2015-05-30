#include "skynet.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <time.h>

struct logger {
	FILE * handle;
	int close;
	time_t now;
	char nowstr[20];
};

struct logger *
logger_create(void) {
	struct logger * inst = skynet_malloc(sizeof(*inst));
	inst->handle = NULL;
	inst->close = 0;
	inst->now = -1;
	inst->nowstr[0] = '\0';
	return inst;
}

void
logger_release(struct logger * inst) {
	if (inst->close) {
		fclose(inst->handle);
	}
	skynet_free(inst);
}

static const char *
_now(struct logger * inst) {
	time_t now;
	struct tm tm_now;
	now = time(NULL);
	if (now == -1) {
		return "TIME ERROR";
	}
	if (now == inst->now) {
		return inst->nowstr;
	}
	if (!localtime_r(&now, &tm_now)) {
		return "TIME ERROR";
	}
	strftime(inst->nowstr, sizeof(inst->nowstr), "%F %T", &tm_now);
	inst->now = now;
	return inst->nowstr;
}

static int
_logger(struct skynet_context * context, void *ud, int type, int session, uint32_t source, const void * msg, size_t sz) {
	struct logger * inst = ud;
	fprintf(inst->handle, "[%s :%08x] ", _now(inst), source);
	fwrite(msg, sz , 1, inst->handle);
	fprintf(inst->handle, "\n");
	fflush(inst->handle);

	return 0;
}

int
logger_init(struct logger * inst, struct skynet_context *ctx, const char * parm) {
	if (parm) {
		inst->handle = fopen(parm,"w");
		if (inst->handle == NULL) {
			return 1;
		}
		inst->close = 1;
	} else {
		inst->handle = stdout;
	}
	if (inst->handle) {
		skynet_callback(ctx, inst, _logger);
		skynet_command(ctx, "REG", ".logger");
		return 0;
	}
	return 1;
}
