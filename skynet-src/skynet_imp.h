#ifndef SKYNET_IMP_H
#define SKYNET_IMP_H

#include <string.h>

struct skynet_config {
	int thread;
	int harbor;
	int profile;
	const char * daemon;
	const char * module_path;
	const char * bootstrap;
	const char * logger;
	const char * logservice;
};

#define THREAD_WORKER 0
#define THREAD_MAIN 1
#define THREAD_SOCKET 2
#define THREAD_TIMER 3
#define THREAD_MONITOR 4

void skynet_start(struct skynet_config * config);

static inline char *
skynet_strndup(const char *str, size_t size) {
	char * ret = skynet_malloc(size+1);
	if (ret == NULL) return NULL;
	memcpy(ret, str, size);
	ret[size] = '\0';
	return ret;
}

static inline char *
skynet_strdup(const char *str) {
	size_t sz = strlen(str);
	return skynet_strndup(str, sz);
}

#endif
