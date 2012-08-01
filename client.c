#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>

#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

static void
test(int fd) {
	char tmp[1024];
	while (!feof(stdin)) {
		fgets(tmp,sizeof(tmp),stdin);
		int n = strlen(tmp) -1;
		uint8_t head[2];
		head[0] = n & 0xff;
		head[1] = (n >> 8) & 0xff;
		int r;
		r = send(fd, head, 2, 0);
		if (r<0) {
			perror("send head");
		}
		r = send(fd, tmp , n, 0);
		if (r<0) {
			perror("send data");
		}
	}
}

int 
main(int argc, char * argv[]) {
	if (argc < 3) {
		printf("connect address port\n");
		return 1;
	}

	int fd = socket(AF_INET,SOCK_STREAM,0);
	struct sockaddr_in my_addr;

	my_addr.sin_addr.s_addr=inet_addr(argv[1]);
	my_addr.sin_family=AF_INET;
	my_addr.sin_port=htons(strtol(argv[2],NULL,10));

	int r = connect(fd,(struct sockaddr *)&my_addr,sizeof(struct sockaddr_in));

	if (r == -1) {
		perror("Connect failed:");
		return 1;
	}

	test(fd);

	close(fd);

	return 0;
}