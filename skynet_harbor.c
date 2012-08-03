#include "skynet_harbor.h"
#include "skynet_mq.h"
#include "skynet_handle.h"
#include "skynet_system.h"
#include "skynet.h"

#include <zmq.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>

#define HASH_SIZE 4096
#define DEFAULT_QUEUE_SIZE 1024

// see skynet_handle.h for HANDLE_REMOTE_SHIFT
#define REMOTE_MAX 255

struct keyvalue {
	struct keyvalue * next;
	uint32_t hash;
	char * key;
	uint32_t value;
	struct message_queue * queue;
};

struct hashmap {
	struct keyvalue *node[HASH_SIZE];
};

struct remote_header {
	uint32_t source;
	uint32_t destination;
};

struct remote {
	void *socket;
	struct message_queue *queue;
};

struct harbor {
	void * zmq_context;
	void * zmq_master_request;
	void * zmq_local;
	void * zmq_queue_notice;
	int notice_event;
	struct hashmap *map;
	struct remote remote[REMOTE_MAX];
	struct message_queue *queue;
	int harbor;

	int lock;
};

static struct harbor *Z = NULL;

// todo: optimize for little endian system

static inline void
buffer_to_remote_header(uint8_t *buffer, struct remote_header *header) {
	header->source = buffer[0] | buffer[1] << 8 | buffer[2] << 16 | buffer[3] << 24;
	header->destination = buffer[4] | buffer[5] << 8 | buffer[6] << 16 | buffer[7] << 24;
}

static inline void
remote_header_to_buffer(struct remote_header *header, uint8_t *buffer) {
	buffer[0] = header->source & 0xff;
	buffer[1] = (header->source >> 8) & 0xff;
	buffer[2] = (header->source >>16) & 0xff;
	buffer[3] = (header->source >>24)& 0xff;
	buffer[4] = (header->destination) & 0xff;
	buffer[5] = (header->destination >>8) & 0xff;
	buffer[6] = (header->destination >>16) & 0xff;
	buffer[7] = (header->destination >>24) & 0xff;
}

static uint32_t
calc_hash(const char *name) {
	int i;
	uint32_t h = 0;
	for (i=0;name[i];i++) {
	    h = h ^ ((h<<5)+(h>>2)+(uint8_t)name[i]);
	}
	h ^= i;
	return h;
}

static inline void
_lock() {
	while (__sync_lock_test_and_set(&Z->lock,1)) {}
}

static inline void
_unlock() {
	__sync_lock_release(&Z->lock);
}

static struct hashmap *
_hash_new(void) {
	struct hashmap * hash = malloc(sizeof(*hash));
	memset(hash, 0, sizeof(*hash));
	return hash;
}

static struct keyvalue *
_hash_search(struct hashmap * hash, const char * key) {
	uint32_t h = calc_hash(key);
	struct keyvalue * n = hash->node[h & (HASH_SIZE-1)];
	while (n) {
		if (n->hash == h && strcmp(n->key, key) == 0) {
			return n;
		}
		n = n->next;
	}
	return NULL;
}

static void
_hash_insert(struct hashmap * hash, const char * key, uint32_t handle, struct message_queue *queue) {
	uint32_t h = calc_hash(key);
	struct keyvalue * node = malloc(sizeof(*node));
	node->next = hash->node[h & (HASH_SIZE-1)];
	node->hash = h;
	node->key = strdup(key);
	node->value = handle;
	node->queue = queue;

	hash->node[h & (HASH_SIZE-1)] = node;
}

static void
send_notice() {
	if (__sync_lock_test_and_set(&Z->notice_event,1)) {
		// already send notice
		return;
	}
	static __thread void * queue_notice = NULL;
	if (queue_notice == NULL) {
		void * pub = zmq_socket(Z->zmq_context, ZMQ_PUSH);
		int r = zmq_connect(pub , "inproc://notice");
		assert(r==0);
		queue_notice = pub;
	}
	zmq_msg_t dummy;
	zmq_msg_init(&dummy);
	zmq_send(queue_notice,&dummy,0);
	zmq_msg_close(&dummy);
}

void 
skynet_harbor_send(const char *name, struct skynet_message * message) {
	if (name == NULL) {
		int remote_id = message->destination >> HANDLE_REMOTE_SHIFT;
		assert(remote_id > 0 && remote_id <= REMOTE_MAX);
		--remote_id;
		struct remote * r = &Z->remote[remote_id];
		if (r->socket) {
			skynet_mq_enter(Z->queue, message);
			send_notice();
		} else {
			skynet_mq_enter(r->queue, message);
		}
	} else {
		_lock();
		struct keyvalue * node = _hash_search(Z->map, name);
		if (node) {
			uint32_t dest = node->value;
			_unlock();
			if (dest == 0) {
				// push message to unknown name service queue
				skynet_mq_enter(node->queue, message);
			} else {
				message->destination = dest;
				if (!skynet_harbor_message_isremote(dest)) {
					// local message
					skynet_mq_push(message);
					return;
				}
				skynet_mq_enter(Z->queue,message);
				send_notice();
			}
		} else {
			// never seen name before
			struct message_queue * queue =  skynet_mq_create(DEFAULT_QUEUE_SIZE);
			skynet_mq_enter(queue, message);
			_hash_insert(Z->map, name, 0, queue);
			_unlock();
			// 0 for query
			skynet_harbor_register(name,0);
		}
	}
}

//queue a register message (destination = 0)
void 
skynet_harbor_register(const char *name, uint32_t handle) {
	struct skynet_message msg;
	msg.source = handle;
	msg.destination = SKYNET_SYSTEM_NAME;
	msg.data = strdup(name);
	msg.sz = 0;
	skynet_mq_enter(Z->queue,&msg);
	send_notice();
}

static void
_register_name(const char *name, uint32_t addr) {
	_lock();
	struct keyvalue * node = _hash_search(Z->map, name);
	if (node) {
		if (node->value) {
			node->value = addr;
			assert(node->queue == NULL);
		} else {
			node->value = addr;
		}
	} else {
		_hash_insert(Z->map, name, addr, NULL);
	}

	struct skynet_message msg;
	struct message_queue * queue = node ? node->queue : NULL;

	if (queue) {
		if (skynet_harbor_message_isremote(addr)) {
			while (skynet_mq_leave(queue, &msg)) {
				msg.destination = addr;
				skynet_mq_enter(Z->queue, &msg);
			}
		} else {
			while (skynet_mq_leave(queue, &msg)) {
				msg.destination = addr;
				skynet_mq_push(&msg);
			}
		}

		node->queue = NULL;
	}

	_unlock();

	if (queue) {
		skynet_mq_release(queue);
	}
}

static void
_remote_harbor_update(int harbor_id, const char * addr) {
	struct remote * r = &Z->remote[harbor_id-1];
	void *socket = zmq_socket( Z->zmq_context, ZMQ_PUSH);
	int rc = zmq_connect(socket, addr);
	if (rc<0) {
		skynet_error(NULL, "Can't connect to %d %s",harbor_id,addr);
		zmq_close(socket);
		socket = NULL;
	}
	if (socket) {
		for (;;) {
			void *old_socket = r->socket;
			if (__sync_bool_compare_and_swap(&r->socket, old_socket, socket)) {
				if (old_socket) {
					zmq_close(old_socket);
				}
				break;
			}
		}
	}
}

static void
_report_zmq_error(int rc) {
	if (rc) {
		fprintf(stderr, "zmq error : %s\n",zmq_strerror(errno));
		exit(1);
	}
}

static void
_name_update() {
	zmq_msg_t content;
	zmq_msg_init(&content);
	int rc = zmq_recv(Z->zmq_local,&content,0);
	_report_zmq_error(rc);
	int sz = zmq_msg_size(&content);
	int i;
	int n = 0;
	uint8_t * buffer = zmq_msg_data(&content);
	for (i=0;i<sz;i++) {
		if (buffer[i] == '=') {
			buffer[i] = '\0';
			break;
		}
		n = n * 10 + (buffer[i] - '0');
	}
	if (i==sz) {
		char tmp[sz+1];
		memcpy(tmp,buffer,sz);
		tmp[sz] = '\0';
		skynet_error(NULL, "Invalid master update [%s]",tmp);
		zmq_msg_close(&content);
		return;
	}

	char tmp[sz-i];
	memcpy(tmp,buffer+i+1,sz-i-1);
	tmp[sz-i-1]='\0';

	if (n>0 && n <= REMOTE_MAX) {
		_remote_harbor_update(n, tmp);
	} else {
		uint32_t source = strtoul(tmp,NULL,16);
		if (source == 0) {
			skynet_error(NULL, "Invalid master update [%s=%s]",(const char *)buffer,tmp);
		} else {
			_register_name((const char *)buffer, source);
		}
	}

	zmq_msg_close(&content);
}

static void
remote_query_harbor(int harbor_id) {
	char tmp[32];
	int sz = sprintf(tmp,"%d",harbor_id);
	zmq_msg_t request;
	zmq_msg_init_size(&request,sz);
	memcpy(zmq_msg_data(&request),tmp,sz);
	zmq_send(Z->zmq_master_request, &request, 0);
	zmq_msg_close(&request);
	zmq_msg_t reply;
	zmq_msg_init(&reply);
	int rc = zmq_recv(Z->zmq_master_request, &reply, 0);
	_report_zmq_error(rc);
	sz = zmq_msg_size(&reply);
	char tmp2[sz+1];
	memcpy(tmp2,zmq_msg_data(&reply),sz);
	tmp2[sz] = '\0';
	_remote_harbor_update(harbor_id, tmp2);
	zmq_msg_close(&reply);
}

// remote message has two part
// when part one is nil (size == 0), part two is name update
// Or part one is source:destination (8 bytes little endian), part two is a binary block for message
static void
_remote_recv() {
	zmq_msg_t header;
	zmq_msg_init(&header);
	int rc = zmq_recv(Z->zmq_local,&header,0);
	_report_zmq_error(rc);
	size_t s = zmq_msg_size(&header);
	if (s!=8) {
		// s should be 0
		if (s>0) {
			char tmp[s+1];
			memcpy(tmp, zmq_msg_data(&header),s);
			tmp[s] = '\0';
			skynet_error(NULL,"Invalid master header [%s]",tmp);
		}
		_name_update();
		return;
	}
	uint8_t * buffer = zmq_msg_data(&header);
	struct remote_header rh;
	buffer_to_remote_header(buffer, &rh);
	zmq_close(&header);

	zmq_msg_t * data = malloc(sizeof(zmq_msg_t));
	zmq_msg_init(data);
	rc = zmq_recv(Z->zmq_local,data,0);
	_report_zmq_error(rc);
	struct skynet_message msg;
	msg.source = rh.source;
	msg.destination = rh.destination;
	msg.data = data;
	msg.sz = zmq_msg_size(data);

	// push remote message to local message queue
	skynet_mq_push(&msg);
}

void * 
skynet_harbor_message_open(struct skynet_message * message) {
	return zmq_msg_data(message->data);
}

void 
skynet_harbor_message_close(struct skynet_message * message) {
	zmq_msg_close(message->data);
}

int 
skynet_harbor_message_isremote(uint32_t handle) {
	int harbor_id = handle >> HANDLE_REMOTE_SHIFT;
	return !(harbor_id == 0 || harbor_id == Z->harbor);
}

static void
_remote_register_name(const char *name, uint32_t source) {
	char tmp[strlen(name) + 20];
	int sz = sprintf(tmp,"%s=%X",name,source);
	zmq_msg_t msg;
	zmq_msg_init_size(&msg,sz);
	memcpy(zmq_msg_data(&msg), tmp , sz);
	zmq_send(Z->zmq_master_request, &msg,0);
	zmq_msg_close(&msg);
	zmq_msg_init(&msg);
	int rc = zmq_recv(Z->zmq_master_request, &msg,0);
	_report_zmq_error(rc);
	zmq_msg_close(&msg);
}

static void
_remote_query_name(const char *name) {
	int sz = strlen(name);
	zmq_msg_t msg;
	zmq_msg_init_size(&msg,sz);
	memcpy(zmq_msg_data(&msg), name , sz);
	zmq_send(Z->zmq_master_request, &msg,0);
	zmq_msg_close(&msg);
	zmq_msg_init(&msg);
	int rc = zmq_recv(Z->zmq_master_request, &msg,0);
	_report_zmq_error(rc);
	sz = zmq_msg_size(&msg);
	char tmp[sz+1];
	memcpy(tmp, zmq_msg_data(&msg),sz);
	tmp[sz] = '\0';

	uint32_t addr = strtoul(tmp,NULL,16);
	_register_name(name,addr);

	zmq_msg_close(&msg);
}

static void 
free_message (void *data, void *hint) {
	free(data);
}
static void
remote_socket_send(void * socket, struct skynet_message *msg) {
	struct remote_header rh;
	rh.source = msg->source;
	rh.destination = msg->destination;
	zmq_msg_t part;
	zmq_msg_init_size(&part,8);
	uint8_t * buffer = zmq_msg_data(&part);
	remote_header_to_buffer(&rh,buffer);
	zmq_send(socket, &part, ZMQ_SNDMORE);
	zmq_msg_close(&part);

	zmq_msg_init_data(&part,msg->data,msg->sz,free_message,NULL);
	zmq_send(socket, &part, 0);
	zmq_msg_close(&part);
}

static void
_remote_send() {
	struct skynet_message msg;
	while (skynet_mq_leave(Z->queue,&msg)) {
_goback:
		if (msg.destination == SKYNET_SYSTEM_NAME) {
			// register name
			const char * name = msg.data;
			if (msg.source) {
				_remote_register_name(name, msg.source);
			} else {
				_remote_query_name(name);
			}

			free(msg.data);
		} else {
			int harbor_id = (msg.destination >> HANDLE_REMOTE_SHIFT);
			assert(harbor_id > 0);
			struct remote * r = &Z->remote[harbor_id-1];
			if (r->socket == NULL) {
				if (r->queue == NULL) {
					r->queue = skynet_mq_create(DEFAULT_QUEUE_SIZE);
					skynet_mq_enter(r->queue, &msg);
					remote_query_harbor(harbor_id);
				} else {
					skynet_mq_enter(r->queue, &msg);
				}
			} else {
				remote_socket_send(r->socket, &msg);
			}
		}
	}
	__sync_lock_release(&Z->notice_event);
	// double check
	if (skynet_mq_leave(Z->queue,&msg)) {
		goto _goback;
	}
}

void *
skynet_harbor_dispatch_thread(void *ud) {
	zmq_pollitem_t items[2];

	items[0].socket = Z->zmq_queue_notice;
	items[0].events = ZMQ_POLLIN;
	items[1].socket = Z->zmq_local;
	items[1].events = ZMQ_POLLIN;

	for (;;) {
		zmq_poll(items,2,-1);
		if (items[0].revents) {
			zmq_msg_t msg;
			zmq_msg_init(&msg);
			int rc = zmq_recv(Z->zmq_queue_notice,&msg,0);
			_report_zmq_error(rc);
			zmq_msg_close(&msg);
			_remote_send();
		}
		if (items[1].revents) {
			_remote_recv();
		}
	}
}

static void
register_harbor(void *request, const char *local, int harbor) {
	char tmp[1024];
	sprintf(tmp,"%d",harbor);
	size_t sz = strlen(tmp);

	zmq_msg_t req;
	zmq_msg_init_size (&req , sz);
	memcpy(zmq_msg_data(&req),tmp,sz);
	zmq_send (request, &req, 0);
	zmq_msg_close (&req);

	zmq_msg_t reply;
	zmq_msg_init (&reply);
	int rc = zmq_recv(request, &reply, 0);
	_report_zmq_error(rc);

	sz = zmq_msg_size (&reply);
	if (sz > 0) {
		memcpy(tmp,zmq_msg_data(&reply),sz);
		tmp[sz] = '\0';
		if (strcmp(tmp,local) != 0) {
			fprintf(stderr, "Harbor %d is already registered by %s [%s]\n", harbor, tmp, local);
			exit(1);
		}
	}
	zmq_msg_close (&reply);

	sprintf(tmp,"%d=%s",harbor,local);
	sz = strlen(tmp);
	zmq_msg_init_size (&req , sz);
	memcpy(zmq_msg_data(&req),tmp,sz);
	zmq_send (request, &req, 0);
	zmq_msg_close (&req);

	zmq_msg_init (&reply);
	rc = zmq_recv (request, &reply, 0);
	_report_zmq_error(rc);

	sz = zmq_msg_size (&reply);
	if (sz > 0) {
		char * buffer = zmq_msg_data(&reply);
		memcpy(tmp,buffer,sz);
		tmp[sz] = '\0';

		if (strcmp(local,tmp) !=0) {
			fprintf(stderr, "Harbor %d is already registered by %s (%s)\n", harbor,tmp,local);
			exit(1);
		}
	}
	zmq_msg_close (&reply);
}

void 
skynet_harbor_init(const char * master, const char *local, int harbor) {
	if (harbor <=0 || harbor>255 || strlen(local) > 512) {
		fprintf(stderr,"Invalid harbor id\n");
		exit(1);
	}
	void *context = zmq_init (1);
	void *request = zmq_socket (context, ZMQ_REQ);
	int r = zmq_connect(request, master);
	if (r<0) {
		fprintf(stderr, "Can't connect to master: %s\n",master);
		exit(1);
	}
	void *harbor_socket = zmq_socket(context, ZMQ_PULL);
	r = zmq_bind(harbor_socket, local);
	if (r<0) {
		fprintf(stderr, "Can't bind to local : %s\n",local);
		exit(1);
	}
	register_harbor(request,local, harbor);
	printf("Start harbor on : %s\n",local);

	struct harbor * h = malloc(sizeof(*h));
	memset(h, 0, sizeof(*h));
	h->zmq_context = context;
	h->zmq_master_request = request;
	h->zmq_local = harbor_socket;
	h->map = _hash_new();
	h->harbor = harbor;
	h->queue = skynet_mq_create(DEFAULT_QUEUE_SIZE);
	h->zmq_queue_notice = zmq_socket(context, ZMQ_PULL);
	r = zmq_bind(h->zmq_queue_notice, "inproc://notice");
	assert(r==0);

	Z = h;
}
