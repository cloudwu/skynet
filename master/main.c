#include "skynet_master.h"
#include <zmq.h>

int 
main (int argc, char * argv[]) {
	const char * default_port = "tcp://127.0.0.1:2012";
	if (argc > 1) {
		default_port = argv[2];
	}

	void *context = zmq_init (1);

	skynet_master(context, default_port);

	zmq_term (context);
	return 0;
}
