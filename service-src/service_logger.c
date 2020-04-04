#include "skynet.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/types.h>

#define maxfilesize  524288000 	//单个文件大小
#define filenamelen  128

struct logger {
	FILE * handle;
	char * filename;
	char * prefix;
	int close;
	int filesize;
	int level;
	time_t filestp;
};

struct logger *
logger_create(void) {
	struct logger * inst = skynet_malloc(sizeof(*inst));
	inst->handle = NULL;
	inst->close = 0;
	inst->filename = NULL;

	inst->filesize = 0;
	inst->level = 0;
	inst->filestp = 0;

	return inst;
}

void
logger_release(struct logger * inst) {
	if (inst->close) {
		fclose(inst->handle);
	}
	skynet_free(inst->filename);
	skynet_free(inst->prefix);
	skynet_free(inst);
}

static int 
checklogfile(struct logger * inst){
	if (inst->handle == NULL){
		return -1;
	}
	if(inst->filesize > maxfilesize){
		return 1;
	}else {
		time_t now = time(NULL);
		static int zone_8_time = 8 * 60 * 60;
		static int day_time = 60 * 60 * 24;
		int d1 = (inst->filestp + zone_8_time) / day_time;
		int d2 = (now + zone_8_time) / day_time;
		if (d1 != d2){
			return 2;
		}
	}
	return 0;
}

static void 
newlogfile(struct logger * inst){
	if(inst->handle){
		fclose(inst->handle);
		inst->handle = NULL;
	}
	if(inst->filename){
		skynet_free(inst->filename);
		inst->filename = NULL;
	}
	inst->filename = skynet_malloc(filenamelen + 1);
	struct tm *local;
	time_t now = time(NULL);
	local = localtime(&now);
	char logdir[256] = { 0 };
	mkdir("log/", 0755);
	sprintf(logdir, "log/%d-%d/", local->tm_year + 1900, local->tm_mon + 1);
	mkdir(logdir, 0755);
	sprintf(inst->filename, "%s%s_%d_%02d_%02d_%02d%02d%02d.log", logdir, inst->prefix, local->tm_year + 1900, local->tm_mon + 1, local->tm_mday, local->tm_hour, local->tm_min, local->tm_sec);

	inst->handle = fopen(inst->filename,"w");
	inst->filestp = now;
}

static int
logger_cb(struct skynet_context * context, void *ud, int type, int session, uint32_t source, const void * msg, size_t sz) {
	struct logger * inst = ud;
	switch (type) {
	case PTYPE_SYSTEM:
		if (inst->filename) {
			inst->handle = freopen(inst->filename, "a", inst->handle);
		}
		break;
	case PTYPE_TEXT:
		{
			int ret = checklogfile(inst);
			if (inst->handle != stdout && ret != 0){
				newlogfile(inst);
			}
			fprintf(inst->handle, "[:%08x] ",source);
			fwrite(msg, sz , 1, inst->handle);
			fprintf(inst->handle, "\n");
			fflush(inst->handle);
			inst->filesize += sz;
			break;
		}
	}

	return 0;
}

int
logger_init(struct logger * inst, struct skynet_context *ctx, const char * parm) {
	if (parm) {
		inst->prefix = skynet_malloc(strlen(parm)+1);
		strcpy(inst->prefix, parm);
		inst->close = 1;
	} else {
		inst->handle = stdout;
	}
	skynet_callback(ctx, inst, logger_cb);
	skynet_command(ctx, "REG", ".logger");
	return 0;
}
