#include "skynet.h"
#include "skynet_harbor.h"

#include <string.h>
#include <assert.h>

#define HASH_SIZE 4096
#define DEFAULT_QUEUE_SIZE 1024

struct msg {
	uint8_t * buffer;
	size_t size;
};

struct msg_queue {
	int size;
	int head;
	int tail;
	struct msg * data;
};

struct keyvalue {
	struct keyvalue * next;
	char key[GLOBALNAME_LENGTH];
	uint32_t hash;
	uint32_t value;
	struct msg_queue * queue;
};

struct hashmap {
	struct keyvalue *node[HASH_SIZE];
};

/*
	message type (8bits) is in destination high 8bits
	harbor id (8bits) is also in that place , but  remote message doesn't need harbor id.
 */
struct remote_message_header {
	uint32_t source;
	uint32_t destination;
	uint32_t session;
};

// 12 is sizeof(struct remote_message_header)
#define HEADER_COOKIE_LENGTH 12

struct dummy {
	struct skynet_context *ctx;
	struct hashmap * map;
};

// hash table

static void
_push_queue(struct msg_queue * queue, const void * buffer, size_t sz, struct remote_message_header * header) {
	// If there is only 1 free slot which is reserved to distinguish full/empty
	// of circular buffer, expand it.
	if (((queue->tail + 1) % queue->size) == queue->head) {
		struct msg * new_buffer = skynet_malloc(queue->size * 2 * sizeof(struct msg));
		int i;
		for (i=0;i<queue->size-1;i++) {
			new_buffer[i] = queue->data[(i+queue->head) % queue->size];
		}
		skynet_free(queue->data);
		queue->data = new_buffer;
		queue->head = 0;
		queue->tail = queue->size - 1;
		queue->size *= 2;
	}
	struct msg * slot = &queue->data[queue->tail];
	queue->tail = (queue->tail + 1) % queue->size;

	slot->buffer = skynet_malloc(sz + sizeof(*header));
	memcpy(slot->buffer, buffer, sz);
	memcpy(slot->buffer + sz, header, sizeof(*header));
	slot->size = sz + sizeof(*header);
}

static struct msg *
_pop_queue(struct msg_queue * queue) {
	if (queue->head == queue->tail) {
		return NULL;
	}
	struct msg * slot = &queue->data[queue->head];
	queue->head = (queue->head + 1) % queue->size;
	return slot;
}

static struct msg_queue *
_new_queue() {
	struct msg_queue * queue = skynet_malloc(sizeof(*queue));
	queue->size = DEFAULT_QUEUE_SIZE;
	queue->head = 0;
	queue->tail = 0;
	queue->data = skynet_malloc(DEFAULT_QUEUE_SIZE * sizeof(struct msg));

	return queue;
}

static void
_release_queue(struct msg_queue *queue) {
	if (queue == NULL)
		return;
	struct msg * m = _pop_queue(queue);
	while (m) {
		skynet_free(m->buffer);
		m = _pop_queue(queue);
	}
	skynet_free(queue->data);
	skynet_free(queue);
}

static struct keyvalue *
_hash_search(struct hashmap * hash, const char name[GLOBALNAME_LENGTH]) {
	uint32_t *ptr = (uint32_t*) name;
	uint32_t h = ptr[0] ^ ptr[1] ^ ptr[2] ^ ptr[3];
	struct keyvalue * node = hash->node[h % HASH_SIZE];
	while (node) {
		if (node->hash == h && strncmp(node->key, name, GLOBALNAME_LENGTH) == 0) {
			return node;
		}
		node = node->next;
	}
	return NULL;
}

static struct keyvalue *
_hash_insert(struct hashmap * hash, const char name[GLOBALNAME_LENGTH]) {
	uint32_t *ptr = (uint32_t *)name;
	uint32_t h = ptr[0] ^ ptr[1] ^ ptr[2] ^ ptr[3];
	struct keyvalue ** pkv = &hash->node[h % HASH_SIZE];
	struct keyvalue * node = skynet_malloc(sizeof(*node));
	memcpy(node->key, name, GLOBALNAME_LENGTH);
	node->next = *pkv;
	node->queue = NULL;
	node->hash = h;
	node->value = 0;
	*pkv = node;

	return node;
}

static struct hashmap * 
_hash_new() {
	struct hashmap * h = skynet_malloc(sizeof(struct hashmap));
	memset(h,0,sizeof(*h));
	return h;
}

static void
_hash_delete(struct hashmap *hash) {
	int i;
	for (i=0;i<HASH_SIZE;i++) {
		struct keyvalue * node = hash->node[i];
		while (node) {
			struct keyvalue * next = node->next;
			_release_queue(node->queue);
			skynet_free(node);
			node = next;
		}
	}
	skynet_free(hash);
}

///////////////

struct dummy *
dummy_create(void) {
	struct dummy * d = skynet_malloc(sizeof(*d));
	d->map = _hash_new();
	return d;
}

void
dummy_release(struct dummy *d) {
	_hash_delete(d->map);
	skynet_free(d);
}

static inline void
to_bigendian(uint8_t *buffer, uint32_t n) {
	buffer[0] = (n >> 24) & 0xff;
	buffer[1] = (n >> 16) & 0xff;
	buffer[2] = (n >> 8) & 0xff;
	buffer[3] = n & 0xff;
}

static inline void
_header_to_message(const struct remote_message_header * header, uint8_t * message) {
	to_bigendian(message , header->source);
	to_bigendian(message+4 , header->destination);
	to_bigendian(message+8 , header->session);
}

static inline uint32_t
from_bigendian(uint32_t n) {
	union {
		uint32_t big;
		uint8_t bytes[4];
	} u;
	u.big = n;
	return u.bytes[0] << 24 | u.bytes[1] << 16 | u.bytes[2] << 8 | u.bytes[3];
}

static inline void
_message_to_header(const uint32_t *message, struct remote_message_header *header) {
	header->source = from_bigendian(message[0]);
	header->destination = from_bigendian(message[1]);
	header->session = from_bigendian(message[2]);
}

static void
_dispatch_queue(struct dummy *h, struct msg_queue * queue, uint32_t handle,  const char name[GLOBALNAME_LENGTH] ) {
	struct msg * m = _pop_queue(queue);
	while (m) {
		struct remote_message_header cookie;
		uint8_t *ptr = m->buffer + m->size - sizeof(cookie);
		memcpy(&cookie, ptr, sizeof(cookie));
		int type = cookie.destination >> HANDLE_REMOTE_SHIFT;
		skynet_send(h->ctx, cookie.source, handle , type | PTYPE_TAG_DONTCOPY, cookie.session, m->buffer, m->size - sizeof(cookie));
		m = _pop_queue(queue);
	}
}

static void
_update_name(struct dummy *h, const char name[GLOBALNAME_LENGTH], uint32_t handle) {
	struct keyvalue * node = _hash_search(h->map, name);
	if (node == NULL) {
		node = _hash_insert(h->map, name);
	}
	node->value = handle;
	if (node->queue) {
		_dispatch_queue(h, node->queue, handle, name);
		_release_queue(node->queue);
		node->queue = NULL;
	}
}

static void
_send_name(struct dummy *h, uint32_t source, const char name[GLOBALNAME_LENGTH], int type, int session, const char * msg, size_t sz) {
	struct keyvalue * node = _hash_search(h->map, name);
	if (node == NULL) {
		node = _hash_insert(h->map, name);
	}
	if (node->value == 0) {
		if (node->queue == NULL) {
			node->queue = _new_queue();
		}
		struct remote_message_header header;
		header.source = source;
		header.destination = type << HANDLE_REMOTE_SHIFT;
		header.session = (uint32_t)session;
		_push_queue(node->queue, msg, sz, &header);
	} else {
		// local message
		skynet_send(h->ctx, source, node->value , type | PTYPE_TAG_DONTCOPY, session, (void *)msg, sz);
	}
}

static int
_mainloop(struct skynet_context * context, void * ud, int type, int session, uint32_t source, const void * msg, size_t sz) {
	struct dummy * h = ud;
	switch (type) {
	case PTYPE_SYSTEM: {
		// register name message
		const struct remote_message *rmsg = msg;
		assert (sz == sizeof(rmsg->destination));
		_update_name(h, rmsg->destination.name, rmsg->destination.handle);
		return 0;
	}
	default: {
		// remote message out
		const struct remote_message *rmsg = msg;
		if (rmsg->destination.handle == 0) {
			_send_name(h, source , rmsg->destination.name, type, session, rmsg->message, rmsg->sz);
		} else {
			// local message
			skynet_send(context, source, rmsg->destination.handle , type | PTYPE_TAG_DONTCOPY, session, (void *)rmsg->message, rmsg->sz);
		}
		return 0;
	}
	}
}

int
dummy_init(struct dummy *d, struct skynet_context *ctx, const char * args) {
	d->ctx = ctx;
	skynet_harbor_start(ctx);
	skynet_callback(ctx, d, _mainloop);

	return 0;
}
