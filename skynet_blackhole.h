#ifndef SKYNET_BLACKHOLE_H
#define SKYNET_BLACKHOLE_H

#include <stdlib.h>

struct blackhole {
	int source;
	char * destination;
	void * data;
	size_t sz;
};

#endif
