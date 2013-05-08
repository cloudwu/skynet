#include <pthread.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>

#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>

struct args {
	int fd;
};

static int
readall(int fd, void * buffer, size_t sz) {
	for (;;) {
		int err = recv(fd , buffer, sz, MSG_WAITALL);
		if (err < 0) {
			if (errno == EAGAIN || errno == EINTR)
				continue;
			break;
		}
		return err;
	}
	perror("Socket error");
	exit(1);
}

static void *
_read(void *ud) {
	struct args *p = ud;
	int fd = p->fd;
	fflush(stdout);
	for (;;) {
		uint8_t header[2];
		fflush(stdout);
		if (readall(fd, header, 2) == 0)
			break;
		size_t len = header[0] << 8 | header[1];
		if (len>0) {
			char tmp[len+1];
			readall(fd, tmp, len);
			tmp[len]='\0';
			printf("%s\n",tmp);
		}
	}
	return NULL;
}

static void *
test(void *ud) {
	struct args *p = ud;
	int fd = p->fd;

	char tmp[1024];
	while (!feof(stdin)) {
		fgets(tmp,sizeof(tmp),stdin);
		int n = strlen(tmp) -1;
		uint8_t head[2];
		head[0] = (n >> 8) & 0xff;
		head[1] = n & 0xff;
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
	return NULL;
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

	struct args arg = { fd };
	pthread_t pid ;
	pthread_create(&pid, NULL, _read, &arg);
	pthread_t pid_stdin;
	pthread_create(&pid_stdin, NULL, test, &arg);

	pthread_join(pid, NULL); 

	close(fd);

	return 0;
}