#include "skynet.h"
#include "skynet_harbor.h"
#include "skynet_socket.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <assert.h>
#include <stdint.h>
#include <unistd.h>


// Harbor 间通过单向的 tcp 连接管道传输数据，完成不同的 skynet 节点间的数据交换。
// skynet 目前支持一个全局名字服务，可以把一个消息包发送到特定名字的服务上。这个服务不必存在于当前 skynet 节点中。这样，我们就需要一个机构能够同步这些全局名字。
// 为此，我实现了一个叫做 master 的服务。它的作用就是广播同步所有的全局名字，以及加入进来的 skynet 节点的地址。本质上，这些地址也是一种名字。
// 同样可以用 key-value 的形式储存。即，每个 skynet 节点号对应一个字符串的地址。

// http://blog.codingnow.com/2012/08/skynet_harbor_rpc.html

// harbor主要用于skynet集群 不同节点间的通信 是skynet集群的通信模块

#define HASH_SIZE          4096
#define DEFAULT_QUEUE_SIZE 1024

struct msg {
	uint8_t * buffer; // buffer分配出来的
	size_t size;
};

// mq
struct msg_queue {
	int size;
	int head;
	int tail;
	struct msg * data; // data[] slot保存了具体的数据而不是指针 这里的实现为固定大小的环形缓冲区 大小不够时是2倍的扩张
};

// key-value map 链表 即 node的结构
// 这里用链地址法解决的 hash冲突问题 所以先找到 bucket再在链表中查找
struct keyvalue {
	struct keyvalue * next;      // 这里用链地址法解决的 hash冲突问题 所以先找到 bucket再在链表中查找
	char key[GLOBALNAME_LENGTH]; // value: name
	uint32_t hash;				 // hash
	uint32_t value;              // key  : handle
	struct msg_queue * queue; 	 // 该链表的每个节点都有一个消息队列mq 用于保存这个节点的消息
};

// map map的每一个节点都有一个消息队列
struct hashmap {
	struct keyvalue *node[HASH_SIZE];
};

/*
	message type (8bits) is in destination high 8bits
	harbor id (8bits) is also in that place , but  remote message doesn't need harbor id.
 */
// 高8位是目的id harbor id也是如此 但是远程的msg不需要harbor id ?

struct remote_message_header {
	uint32_t source;
	uint32_t destination;
	uint32_t session;
};

// harbor的结构 harbor保存了本集群所有节点的通信地址 skynet集群内部会简历 n*n个节点 相当于每个节点间都建立了tcp连接
struct harbor {
	struct skynet_context *ctx; 	// skynet_ctx
	char * local_addr;				// 本地地址
	int id;                         // id
	struct hashmap * map;           // hashmap
	int master_fd;              	// master_fd
	char * master_addr;             // master_addr
	int remote_fd[REMOTE_MAX];  	// remote_fd[]
	bool connected[REMOTE_MAX];     // 标示remote_fd[]是否处于连接状态
	char * remote_addr[REMOTE_MAX]; // remote_addr[]
};

// hash table

// 队列的操作
// ---------------------
static void
_push_queue(struct msg_queue * queue, const void * buffer, size_t sz, struct remote_message_header * header) {
	// If there is only 1 free slot which is reserved to distinguish full/empty
	// of circular buffer, expand it.
	// 环形队列 满了就扩大到2倍
	if (((queue->tail + 1) % queue->size) == queue->head) {
		struct msg * new_buffer = malloc(queue->size * 2 * sizeof(struct msg));
		int i;
		for (i=0;i<queue->size-1;i++) { // copy old data to new mem
			new_buffer[i] = queue->data[(i+queue->head) % queue->size];
		}

		free(queue->data);
		queue->data = new_buffer;
		queue->head = 0;
		queue->tail = queue->size - 1;
		queue->size *= 2;
	}

	struct msg * slot = &queue->data[queue->tail]; // 找到这个队列的slot放入这个msg
	queue->tail = (queue->tail + 1) % queue->size;

	slot->buffer = malloc(sz + sizeof(*header));
	memcpy(slot->buffer, buffer, sz); // 将buffer拷贝到slot中 slot是保存了真正的数据 而不是仅仅指针
	memcpy(slot->buffer + sz, header, sizeof(*header));
	slot->size = sz + sizeof(*header);
}

static struct msg *
_pop_queue(struct msg_queue * queue) {
	if (queue->head == queue->tail) {
		return NULL;
	}

	struct msg * slot = &queue->data[queue->head];
	queue->head = (queue->head + 1) % queue->size; // 环形队列
	return slot;
}

static struct msg_queue *
_new_queue() {
	struct msg_queue * queue = malloc(sizeof(*queue));
	queue->size = DEFAULT_QUEUE_SIZE;
	queue->head = 0;
	queue->tail = 0;
	queue->data = malloc(DEFAULT_QUEUE_SIZE * sizeof(struct msg)); // 默认队列大小为1024的环形队列

	return queue;
}

// 释放队列中所有的节点
static void
_release_queue(struct msg_queue *queue) {
	if (queue == NULL)
		return;
	struct msg * m = _pop_queue(queue);
	while (m) {
		free(m->buffer);
		m = _pop_queue(queue);
	}

	free(queue->data);
	free(queue);
}

// hash的操作
// -------------------
// find name
static struct keyvalue *
_hash_search(struct hashmap * hash, const char name[GLOBALNAME_LENGTH]) {
	uint32_t *ptr = (uint32_t*) name;
	uint32_t h = ptr[0] ^ ptr[1] ^ ptr[2] ^ ptr[3];
	struct keyvalue * node = hash->node[h % HASH_SIZE]; // 这里用链地址法解决的 hash冲突问题 所以先找到 bucket再在链表中查找
	while (node) {
		if (node->hash == h && strncmp(node->key, name, GLOBALNAME_LENGTH) == 0) {
			return node;
		}
		node = node->next;
	}
	return NULL;
}

/*

// Don't support erase name yet 暂时不支持name剔除

static struct void
_hash_erase(struct hashmap * hash, char name[GLOBALNAME_LENGTH) {
	uint32_t *ptr = name;
	uint32_t h = ptr[0] ^ ptr[1] ^ ptr[2] ^ ptr[3];
	struct keyvalue ** ptr = &hash->node[h % HASH_SIZE];
	while (*ptr) {
		struct keyvalue * node = *ptr;
		if (node->hash == h && strncmp(node->key, name, GLOBALNAME_LENGTH) == 0) {
			_release_queue(node->queue);
			*ptr->next = node->next;
			free(node);
			return;
		}
		*ptr = &(node->next);
	}
}
*/

// insert name to hash map
static struct keyvalue *
_hash_insert(struct hashmap * hash, const char name[GLOBALNAME_LENGTH]) {
	uint32_t *ptr = (uint32_t *)name;
	uint32_t h = ptr[0] ^ ptr[1] ^ ptr[2] ^ ptr[3];

	struct keyvalue ** pkv = &hash->node[h % HASH_SIZE]; // 这个bucket的头
	struct keyvalue * node = malloc(sizeof(*node));      // 新分配一个 node 出来 防止 name

	memcpy(node->key, name, GLOBALNAME_LENGTH);
	node->next = *pkv;  // 头插法插入链表中
	node->queue = NULL;
	node->hash = h;
	node->value = 0;    // handle为 0
	*pkv = node;

	return node;
}

// new hash map
static struct hashmap * 
_hash_new() {
	struct hashmap * h = malloc(sizeof(struct hashmap));
	memset(h,0,sizeof(*h));
	return h;
}

// delete hash map and free map node
static void
_hash_delete(struct hashmap *hash) {
	int i;
	for (i=0;i<HASH_SIZE;i++) {
		struct keyvalue * node = hash->node[i];
		while (node) {
			struct keyvalue * next = node->next;
			_release_queue(node->queue); // 释放队列
			free(node);
			node = next;
		}
	}
	free(hash);
}

// ----
// skynet harbor模块的接口
struct harbor *
harbor_create(void) {
	struct harbor * h = malloc(sizeof(*h));
	h->ctx = NULL;
	h->id = 0;
	h->master_fd = -1;
	h->master_addr = NULL;
	int i;
	for (i=0;i<REMOTE_MAX;i++) {
		h->remote_fd[i] = -1;
		h->connected[i] = false;
		h->remote_addr[i] = NULL;
	}
	h->map = _hash_new(); // new hash map
	return h;
}

// harbor_release
void
harbor_release(struct harbor *h) {
	struct skynet_context *ctx = h->ctx;
	if (h->master_fd >= 0) {
		skynet_socket_close(ctx, h->master_fd);
	}
	free(h->master_addr);
	free(h->local_addr);

	int i;
	for (i=0;i<REMOTE_MAX;i++) {
		if (h->remote_fd[i] >= 0) {
			skynet_socket_close(ctx, h->remote_fd[i]);
			free(h->remote_addr[i]);
		}
	}

	_hash_delete(h->map);
	free(h);
}

// connect
static int
_connect_to(struct harbor *h, const char *ipaddress, bool blocking) {
	char * port = strchr(ipaddress,':'); // strchr函数原型：extern char *strchr(const char *s,char c);查找字符串s中首次出现字符c的位置。
	if (port==NULL) {
		return -1;
	}

	int sz = port - ipaddress;
	char tmp[sz + 1];
	memcpy(tmp,ipaddress,sz); // tmp : ip
	tmp[sz] = '\0';

	int portid = strtol(port+1, NULL,10); // 10进制的port

	skynet_error(h->ctx, "Harbor(%d) connect to %s:%d", h->id, tmp, portid);

	// 阻塞还是非阻塞 connect master使用了阻塞
	if (blocking) {
		return skynet_socket_block_connect(h->ctx, tmp, portid);
	}
	else {
		return skynet_socket_connect(h->ctx, tmp, portid);
	}
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

	u.big = n; // y?
	return u.bytes[0] << 24 | u.bytes[1] << 16 | u.bytes[2] << 8 | u.bytes[3];
}

static inline void
_message_to_header(const uint32_t *message, struct remote_message_header *header) {
	header->source = from_bigendian(message[0]);
	header->destination = from_bigendian(message[1]);
	header->session = from_bigendian(message[2]);
}

// 发包出去
static void
_send_package(struct skynet_context *ctx, int fd, const void * buffer, size_t sz) {
	uint8_t * sendbuf = malloc(sz+4);
	to_bigendian(sendbuf, sz);
	memcpy(sendbuf+4, buffer, sz); // 发包的时候 包头先压入了4个字节的包体大小

	if (skynet_socket_send(ctx, fd, sendbuf, sz+4)) {
		skynet_error(ctx, "Send to %d error", fd);
	}
}

// 向远程fd发送消息
static void
_send_remote(struct skynet_context * ctx, int fd, const char * buffer, size_t sz, struct remote_message_header * cookie) {
	uint32_t sz_header = sz+sizeof(*cookie);
	uint8_t * sendbuf = malloc(sz_header+4);
	to_bigendian(sendbuf, sz_header);
	memcpy(sendbuf+4, buffer, sz);
	_header_to_message(cookie, sendbuf+4+sz);

	if (skynet_socket_send(ctx, fd, sendbuf, sz_header+4)) {
		skynet_error(ctx, "Remote send to %d error", fd);
	}
}

// harbor id[0, 255] 更新远程harbor的address
static void
_update_remote_address(struct harbor *h, int harbor_id, const char * ipaddr) {
	if (harbor_id == h->id) {
		return;
	}

	assert(harbor_id > 0  && harbor_id< REMOTE_MAX);

	struct skynet_context * context = h->ctx;
	if (h->remote_fd[harbor_id] >=0) {
		skynet_socket_close(context, h->remote_fd[harbor_id]);
		free(h->remote_addr[harbor_id]);
		h->remote_addr[harbor_id] = NULL;
	}

	h->remote_fd[harbor_id] = _connect_to(h, ipaddr, false); // 非阻塞连接 remote server
	h->connected[harbor_id] = false;
}

static void
_dispatch_queue(struct harbor *h, struct msg_queue * queue, uint32_t handle,  const char name[GLOBALNAME_LENGTH] ) {
	int harbor_id = handle >> HANDLE_REMOTE_SHIFT; // // 远程 id 需要偏移 24位得到 它使用了高8位
	assert(harbor_id != 0);
	struct skynet_context * context = h->ctx;

	int fd = h->remote_fd[harbor_id];
	if (fd < 0) {
		char tmp [GLOBALNAME_LENGTH+1];
		memcpy(tmp, name , GLOBALNAME_LENGTH);
		tmp[GLOBALNAME_LENGTH] = '\0';
		skynet_error(context, "Drop message to %s (in harbor %d)",tmp,harbor_id);
		return;
	}

	struct msg * m = _pop_queue(queue);
	while (m) {
		struct remote_message_header cookie;
		uint8_t *ptr = m->buffer + m->size - sizeof(cookie);
		memcpy(&cookie, ptr, sizeof(cookie));
		cookie.destination |= (handle & HANDLE_MASK);

		_header_to_message(&cookie, ptr); // cookie copy to ptr buffer

		_send_package(context, fd, m->buffer, m->size); // 将这个消息发送出去 发送到对应的fd
		m = _pop_queue(queue); // 继续弹出消息
	}
}

static void
_update_remote_name(struct harbor *h, const char name[GLOBALNAME_LENGTH], uint32_t handle) {
	struct keyvalue * node = _hash_search(h->map, name); // 查找这个远程主机
	if (node == NULL) {
		node = _hash_insert(h->map, name); // 将这个name[]插入到hash_map中
	}

	node->value = handle;
	if (node->queue) {
		_dispatch_queue(h, node->queue, handle, name); // 将这个节点保存的消息发送出去
		_release_queue(node->queue); // 释放这个节点保存消息的队列
		node->queue = NULL;
	}
}

// 将这个 handle的信息同步到master中
static void
_request_master(struct harbor *h, const char name[GLOBALNAME_LENGTH], size_t i, uint32_t handle) {
	uint8_t buffer[4+i]; 		  // handle是uint32_t 所以需要4个uint8_t来存放这个handle
	to_bigendian(buffer, handle); // buffer前32位放了handle
	memcpy(buffer+4,name,i);

	_send_package(h->ctx, h->master_fd, buffer, 4+i); // 将这个 handle和对应的addr(ip:port)的信息同步到master中
}

/*
	update global name to master

	2 bytes (size)
	4 bytes (handle) (handle == 0 for request)
	n bytes string (name)
 */

// 向全局的 master服务更新节点name
static int
_remote_send_handle(struct harbor *h, uint32_t source, uint32_t destination, int type, int session, const char * msg, size_t sz) {
	int harbor_id = destination >> HANDLE_REMOTE_SHIFT;
	assert(harbor_id != 0);
	struct skynet_context * context = h->ctx;

	// 本地消息
	if (harbor_id == h->id) {
		// local message
		skynet_send(context, source, destination , type | PTYPE_TAG_DONTCOPY, session, (void *)msg, sz);
		return 1;
	}

	// 远程消息
	int fd = h->remote_fd[harbor_id];
	if (fd >= 0 && h->connected[harbor_id]) {
		struct remote_message_header cookie;
		cookie.source = source;
		cookie.destination = (destination & HANDLE_MASK) | ((uint32_t)type << HANDLE_REMOTE_SHIFT);
		cookie.session = (uint32_t)session;
		_send_remote(context, fd, msg,sz,&cookie);
	}
	else {
		// throw an error return to source
		if (session != 0) {
			skynet_send(context, destination, source, PTYPE_RESERVED_ERROR, session, NULL, 0);
		}
		skynet_error(context, "Drop message to harbor %d from %x to %x (session = %d, msgsz = %d)",harbor_id, source, destination,session,(int)sz);
	}
	return 0;
}

// 向master注册新的name和handle
static void
_remote_register_name(struct harbor *h, const char name[GLOBALNAME_LENGTH], uint32_t handle) {
	int i;
	for (i=0;i<GLOBALNAME_LENGTH;i++) {
		if (name[i] == '\0')
			break;
	}
	if (handle != 0) {
		_update_remote_name(h, name, handle); // 更新 remote_name map
	}

	_request_master(h, name,i,handle); // 将这个节点更新到master服务
}

static int
_remote_send_name(struct harbor *h, uint32_t source, const char name[GLOBALNAME_LENGTH], int type, int session, const char * msg, size_t sz) {
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
		// 0 for request

		// 向master注册新的name和handle
		_remote_register_name(h, name, 0);
		return 1;
	} else {
		return _remote_send_handle(h, source, node->value, type, session, msg, sz);
	}
}

static int
harbor_id(struct harbor *h, int fd) {
	int i;
	for (i=1;i<REMOTE_MAX;i++) {
		if (h->remote_fd[i] == fd)
			return i;
	}
	return 0; // 在远程fd中没找到这个fd 返回0
}

static void
close_harbor(struct harbor *h, int fd) {
	int id = harbor_id(h,fd);
	if (id == 0)
		return;
	skynet_error(h->ctx, "Harbor %d closed",id);
	skynet_socket_close(h->ctx, fd); // 关闭这个socket fd
	h->remote_fd[id] = -1;
	h->connected[id] = false;
}

static void
open_harbor(struct harbor *h, int fd) {
	int id = harbor_id(h,fd);
	if (id == 0)
		return;
	assert(h->connected[id] == false);
	h->connected[id] = true;
}

static int
_mainloop(struct skynet_context * context, void * ud, int type, int session, uint32_t source, const void * msg, size_t sz) {
	struct harbor * h = ud;
	switch (type) {

	// socket type
	case PTYPE_SOCKET: {
		const struct skynet_socket_message * message = msg;
		switch(message->type) {
		case SKYNET_SOCKET_TYPE_DATA:
			free(message->buffer);
			skynet_error(context, "recv invalid socket message (size=%d)", message->ud);
			break;
		case SKYNET_SOCKET_TYPE_ACCEPT:
			skynet_error(context, "recv invalid socket accept message");
			break;
		case SKYNET_SOCKET_TYPE_ERROR:
		case SKYNET_SOCKET_TYPE_CLOSE:   // socket类型为close 的时候 close harbor
			close_harbor(h, message->id);
			break;
		case SKYNET_SOCKET_TYPE_CONNECT: // connect 的时候 open_harbor()
			open_harbor(h, message->id); // 设置这个 消息要发送的fd为tcp连接状态
			break;
		}
		return 0;
	}

	// harbor type 即远程消息 发给某个 远程主机
	case PTYPE_HARBOR: {
		// remote message in
		const char * cookie = msg;
		cookie += sz - 12; // 3*4 = 12 sizeof(struct remote_message_header) = 12
		struct remote_message_header header;
		_message_to_header((const uint32_t *)cookie, &header);
		if (header.source == 0) {
			if (header.destination < REMOTE_MAX) { // 远程主机hash_map中有这个记录了 重新连接下
				// 1 byte harbor id (0~255)
				// update remote harbor address
				char ip [sz - 11];
				memcpy(ip, msg, sz-12);
				ip[sz-11] = '\0';

				_update_remote_address(h, header.destination, ip); // harbor id[0, 255] 更新远程harbor的address
			}
			else {
				// 如果这个远程主机没有加入到 hash_map 中管理 则将其加入到 hash_map 的名字中管理
				// update global name
				if (sz - 12 > GLOBALNAME_LENGTH) {
					char name[sz-11];
					memcpy(name, msg, sz-12);
					name[sz-11] = '\0';
					skynet_error(context, "Global name is too long %s", name);
				}

				_update_remote_name(h, msg, header.destination);
			}
		}
		// header.source != 0 从一个handle发送到另外一个handle
		else {
			uint32_t destination = header.destination;
			int type = (destination >> HANDLE_REMOTE_SHIFT) | PTYPE_TAG_DONTCOPY;
			destination = (destination & HANDLE_MASK) | ((uint32_t)h->id << HANDLE_REMOTE_SHIFT);

			skynet_send(context, header.source, destination, type, (int)header.session, (void *)msg, sz-12);
			return 1;
		}
		return 0;
	}

	// system type
	case PTYPE_SYSTEM: {
		// register name message
		const struct remote_message *rmsg = msg;
		assert (sz == sizeof(rmsg->destination));
		_remote_register_name(h, rmsg->destination.name, rmsg->destination.handle); // 注册name和对应的handle
		return 0;
	}

	// other remote msg out
	default: {
		// remote message out
		const struct remote_message *rmsg = msg;
		if (rmsg->destination.handle == 0) {
			if (_remote_send_name(h, source , rmsg->destination.name, type, session, rmsg->message, rmsg->sz)) {
				return 0;
			}
		} else {
			if (_remote_send_handle(h, source , rmsg->destination.handle, type, session, rmsg->message, rmsg->sz)) {
				return 0;
			}
		}
		free((void *)rmsg->message);
		return 0;
	}
	}
}

static void
_launch_gate(struct skynet_context * ctx, const char * local_addr) {
	char tmp[128];
	sprintf(tmp,"gate L ! %s %d %d 0",local_addr, PTYPE_HARBOR, REMOTE_MAX);

	// 得到gate服务的handle
	const char * gate_addr = skynet_command(ctx, "LAUNCH", tmp);
	if (gate_addr == NULL) {
		fprintf(stderr, "Harbor : launch gate failed\n");
		exit(1);
	}

	uint32_t gate = strtoul(gate_addr+1 , NULL, 16);
	if (gate == 0) {
		fprintf(stderr, "Harbor : launch gate invalid %s", gate_addr);
		exit(1);
	}

	// 得到自身的handle
	const char * self_addr = skynet_command(ctx, "REG", NULL);
	int n = sprintf(tmp,"broker %s",self_addr);

	// send to 这里使用的简单文本协议 PTYPE_TEXT
	skynet_send(ctx, 0, gate, PTYPE_TEXT, 0, tmp, n);
	skynet_send(ctx, 0, gate, PTYPE_TEXT, 0, "start", 5);
}

// harbor module init 函数
int
harbor_init(struct harbor *h, struct skynet_context *ctx, const char * args) {
	h->ctx = ctx;
	int sz = strlen(args)+1; // inlcude \0
	char master_addr[sz];
	char local_addr[sz];
	int harbor_id = 0;

	sscanf(args,"%s %s %d",master_addr, local_addr, &harbor_id); // sscanf()将args里面的数据格式化输出到这3个值中
	h->master_addr = strdup(master_addr);
	// strdup()将串拷贝到新建的位置处
	// strdup()在内部调用了malloc()为变量分配内存，不需要使用返回的字符串时，需要用free()释放相应的内存空间，否则会造成内存泄漏。

	h->master_fd = _connect_to(h, master_addr, true); // 阻塞连接 master服务器
	if (h->master_fd == -1) {
		fprintf(stderr, "Harbor: Connect to master failed\n");
		exit(1);
	}
	h->local_addr = strdup(local_addr);
	h->id = harbor_id;

	// 启动gate服务 即tcp服务
	_launch_gate(ctx, local_addr);
	skynet_callback(ctx, h, _mainloop); // 设置这个模块的回调函数
	_request_master(h, local_addr, strlen(local_addr), harbor_id); // 向master同步这个handle即harbor_id

	return 0;
}
