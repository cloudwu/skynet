#ifndef SKYNET_IMP_H
#define SKYNET_IMP_H

struct skynet_config {
	int thread;
	int mqueue_size;
	const char * logger;
	const char * module_path;
};

void skynet_start(struct skynet_config * config);

#endif
