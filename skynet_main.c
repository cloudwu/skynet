#include "skynet_imp.h"
#include <stdlib.h>

int
main(void) {
	struct skynet_config config;

	config.thread = 8;
	config.mqueue_size = 256;
	config.module_path = "./";
	config.logger = NULL;

	skynet_start(&config);

	return 0;
}
