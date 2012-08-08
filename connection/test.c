#include "connection.h"

#include <stdio.h>
#include <stdint.h>

static void
test(struct connection_pool *p, int id) {
	int i=0;
	while (i<5) {
		int handle = id;
		const char * line = connection_readline(p, handle, "\n",  NULL);
		if (line == NULL) {
			line = connection_poll(p,1000,&handle,NULL);
		}
		if (line) {
			printf("%d %d: %s\n",i,handle, line);
			connection_write(p,handle,"readline\n",9);
			++i;
		} else {
			if (handle) {
				printf("Close %d\n",handle);
				return;
			}
		}
	}
	i=0;
	while (i<5) {
		int handle = id;
		uint8_t * buffer = connection_read(p, handle, 8);
		if (buffer == NULL) {
			buffer = connection_poll(p,1000,&handle,NULL);
		}
		if (buffer) {
			int j;
			printf("%d %d: ",i,handle);
			for (j=0;j<8;j++) {
				printf("%02x ",buffer[j]);
			}
			printf("\n");
			connection_write(p,handle,"readblock\n",10);
			++i;
		} else {
			if (handle) {
				printf("Close %d\n", handle);
				return;
			}
		}
	}
}

int
main() {
	struct connection_pool * p = connection_newpool(16);

	int handle = connection_open(p, "127.0.0.1:8888");

	test(p,handle);

	connection_deletepool(p);
	
	return 0;
};

