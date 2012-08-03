#include <zmq.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>

#define HASH_SIZE 4096
#define MAX_SLAVE 255

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

struct keyvalue {
	struct keyvalue * next;
	uint32_t hash;
	char * key;
	size_t value_size;
	char * value;
};

struct hashmap {
	struct keyvalue *node[HASH_SIZE];
	void * zmq;
	void * slave[MAX_SLAVE];
};

static struct hashmap *
_hash_new(void *zmq) {
	struct hashmap * hash = malloc(sizeof(*hash));
	memset(hash, 0, sizeof(*hash));
	hash->zmq = zmq;
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
_hash_insert(struct hashmap * hash, const char * key, const char *value) {
	uint32_t h = calc_hash(key);
	struct keyvalue * node = malloc(sizeof(*node));
	node->next = hash->node[h & (HASH_SIZE-1)];
	node->hash = h;
	node->key = strdup(key);
	node->value_size = strlen(value);
	node->value = malloc(node->value_size+1);
	memcpy(node->value, value, node->value_size+1);

	hash->node[h & (HASH_SIZE-1)] = node;
}

static void
_hash_delete(struct hashmap * hash, const char * key) {
	uint32_t h = calc_hash(key);
	struct keyvalue ** ptr = &hash->node[h & (HASH_SIZE-1)];
	while(*ptr) {
		struct keyvalue *n = *ptr;
		if (n->hash == h && strcmp(n->key, key) == 0) {
			*ptr = n->next;
			free(n->key);
			free(n->value);
			free(n);
			return ;
		}
		ptr = &(n->next);
	}
}

static int
_hash_bind(struct hashmap * hash, int slave, const char *addr) {
	void *pub = NULL;
	if (addr) {
		pub = zmq_socket(hash->zmq, ZMQ_PUSH);
		int r = zmq_connect(pub, addr);
		if (r<0) {
			fprintf(stderr,"Can't connect to [%d] %s\n",slave,addr);
			return -1;
		}
		printf("Connect to [%d] %s\n",slave,addr);
	}
	void * old_pub = hash->slave[slave-1];
	hash->slave[slave-1] = pub;
	if (old_pub) {
		zmq_close(old_pub);
	}

	return 0;
}

static void
broadcast(struct hashmap * hash, zmq_msg_t * msg) {
	int i;
	for (i=0;i<MAX_SLAVE;i++) {
		void * pub = hash->slave[i];
		if (pub) {
			zmq_msg_t part;
			zmq_msg_init(&part);
			int rc = zmq_send(pub, &part, ZMQ_SNDMORE);
			if (rc != 0) {
				fprintf(stderr,"Can't publish to %d : %s",i+1,zmq_strerror(errno));
			}
			zmq_msg_close(&part);
			zmq_msg_init(&part);
			zmq_msg_copy(&part,msg);
			rc = zmq_send(pub, &part, 0);
			if (rc != 0) {
				fprintf(stderr,"Can't publish to %d : %s",i+1,zmq_strerror(errno));
			}
			zmq_msg_close(&part);
		}
	}
}

static void
replace(void * responder, struct hashmap * map, const char *key, const char *value) {
//	printf("Replace %s %s\n",key,value);
	struct keyvalue * node = _hash_search(map, key);
	zmq_msg_t reply;
	if (node == NULL) {
		_hash_insert(map, key, value);
		zmq_msg_init_size (&reply, 0);
	} else {
		zmq_msg_init_size (&reply, node->value_size);
		memcpy (zmq_msg_data (&reply), node->value, node->value_size);

		free(node->value);
		node->value_size = strlen(value);
		node->value = malloc(node->value_size + 1);
		memcpy(node->value, value, node->value_size +1);
	}
	zmq_send (responder, &reply, 0);
	zmq_msg_close (&reply);
}

static void
erase(void * responder, struct hashmap *map, const char *key) {
//	printf("Erase %s\n",key);
	struct keyvalue * node = _hash_search(map, key);
	zmq_msg_t reply;
	if (node == NULL) {
		zmq_msg_init_size (&reply, 0);
	} else {
		zmq_msg_init_size (&reply, node->value_size);
		memcpy (zmq_msg_data (&reply), node->value, node->value_size);
		_hash_delete(map, key);
	}
	zmq_send (responder, &reply, 0);
	zmq_msg_close (&reply);
}

static void
query(void * responder, struct hashmap *map, const char * key) {
	struct keyvalue * node = _hash_search(map, key);
	zmq_msg_t reply;
	if (node == NULL) {
		zmq_msg_init_size (&reply, 0);
	} else {
		zmq_msg_init_size (&reply, node->value_size);
		memcpy (zmq_msg_data (&reply), node->value, node->value_size);
	}
	zmq_send (responder, &reply, 0);
	zmq_msg_close (&reply);
}

static void
update(void * responder, struct hashmap *map, zmq_msg_t * msg) {
	size_t sz = zmq_msg_size (msg);
	const char * command = zmq_msg_data (msg);
	int i;
	for (i=0;i<sz;i++) {
		if (command[i] == '=') {
			char key[i+1];
			memcpy(key, command, i);
			key[i] = '\0';
			int slave = strtol(key, NULL, 10);
			if (sz-i == 1) {
				if (slave > 0 && slave <= MAX_SLAVE) {
					_hash_bind(map, slave, NULL);
				}
				erase(responder, map,key);
				broadcast(map, msg);
			} else {
				char value[sz-i];
				memcpy(value, command+i+1, sz-i-1);
				value[sz-i-1] = '\0';
				if (slave > 0 && slave <= MAX_SLAVE) {
					_hash_bind(map, slave, value);
				}

				replace(responder, map, key,value);
				broadcast(map, msg);
			}
			return;
		}
	}
	char key[sz+1];
	memcpy(key, command, sz);
	key[sz] = '\0';
	query(responder, map, key);
}

int 
main (int argc, char * argv[]) {
	const char * default_port = "tcp://127.0.0.1:2012";
	if (argc > 1) {
		default_port = argv[2];
	}

	void *context = zmq_init (1);
	void *responder = zmq_socket (context, ZMQ_REP);

	int r = zmq_bind(responder, default_port);
	if (r < 0) {
		fprintf(stderr, "Can't bind to %s\n",default_port);
		return 1;
	}
	printf("Start master on %s\n",default_port);

	struct hashmap *map = _hash_new(context);

	for (;;) {
		zmq_msg_t request;
		zmq_msg_init (&request);
		zmq_recv (responder, &request, 0);
		update(responder, map, &request);
		zmq_msg_close (&request);
	}

	zmq_close (responder);
	zmq_term (context);
	return 0;
}
