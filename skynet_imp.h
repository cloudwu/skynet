#ifndef SKYNET_IMP_H
#define SKYNET_IMP_H

struct skynet_config {
	int thread;
	int mqueue_size;
	int harbor;
	const char * logger;
	const char * module_path;
	const char * master;
	const char * local;
	const char * start;
};

void skynet_start(struct skynet_config * config);

#endif
