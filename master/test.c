#include <zmq.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <assert.h>

static char *
send_command(void * request, const char * command) {
	size_t size = strlen(command);
	zmq_msg_t req;
	zmq_msg_init_size(&req, size);
	memcpy(zmq_msg_data(&req) , command, size);
	zmq_send (request, &req, 0);
	zmq_msg_close (&req);

	zmq_msg_t reply;
	zmq_msg_init (&reply);
	zmq_recv (request, &reply, 0);

	size = zmq_msg_size (&reply);

	char * ret = malloc(size+1);
	memcpy(ret,zmq_msg_data(&reply),size);
	ret[size]='\0';
	zmq_msg_close (&reply);

	return ret;
}

static void
pull_sub(void * local) {
	zmq_msg_t part;
	zmq_msg_init (&part);
	zmq_recv (local, &part, 0);
	zmq_msg_close(&part);
	zmq_msg_init (&part);
	zmq_recv (local, &part, 0);
	size_t sz = zmq_msg_size(&part);
	char tmp[sz+1];
	memcpy(tmp,zmq_msg_data(&part),sz);
	tmp[sz]='\0';
	printf("%s\n",tmp);
	zmq_msg_close(&part);
}

int 
main (int argc, char * argv[]) {
	const char * default_master = "tcp://127.0.0.1:2012";
	int slave = 100;
	const char * default_local = "tcp://127.0.0.1:5001";
	if (argc > 1) {
		default_master = argv[1];
		if (argc > 2) {
			slave = strtol(argv[2],NULL,10);
			if (slave < 1 || slave> 255) {
				fprintf(stderr,"Slave id must be in [1,255]\n");
				return 1;
			}
			if (argc > 3) {
				default_local = argv[3];
			}
		}
	}

	printf("master = %s\n",default_master);
	printf("local = [%d]%s\n",slave,default_local);

	void *context = zmq_init (1);
	void *request = zmq_socket (context, ZMQ_REQ);
	void *local = zmq_socket( context, ZMQ_PULL);

	int r = zmq_connect(request, default_master);
	if (r < 0) {
		fprintf(stderr, "Can't connect to %s\n",default_master);
		return 1;
	}
	r = zmq_bind(local, default_local);
	if (r < 0) {
		fprintf(stderr, "Can't bind to %s\n",default_local);
		return 1;
	}

	char tmp[1024];
	sprintf(tmp,"%d=%s",slave,default_local);

	char * result = send_command(request,tmp);
	free(result);

	zmq_pollitem_t items[2];
	items[0].socket = local;
	items[0].events = ZMQ_POLLIN;
	items[1].socket = NULL;
	items[1].fd = STDIN_FILENO;
	items[1].events = ZMQ_POLLIN;

	for (;;) {
		int rc = zmq_poll(items,2,-1);
		assert (rc >= 0);
		if (items[0].revents) {
			pull_sub(local);
		}
		if (items[1].revents) {
			char tmp[1024];
			fgets(tmp,sizeof(tmp),stdin);
			char * cr = strchr(tmp,'\r');
			if (cr) {
				*cr = '\0';
			}
			cr = strchr(tmp,'\n');
			if (cr) {
				*cr = '\0';
			}
			result = send_command(request, tmp);
			printf("%s\n",result);
			free(result);
		}
	}

	zmq_close (request);
	zmq_term (context);
	return 0;
}
