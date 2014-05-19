#include "skynet.h"
#include "skynet_harbor.h"
#include "skynet_socket.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <assert.h>
#include <stdint.h>


// Harbor 间通过单向的 tcp 连接管道传输数据，完成不同的 skynet 节点间的数据交换。
// skynet 目前支持一个全局名字服务，可以把一个消息包发送到特定名字的服务上。这个服务不必存在于当前 skynet 节点中。这样，我们就需要一个机构能够同步这些全局名字。
// 为此，我实现了一个叫做 master 的服务。它的作用就是广播同步所有的全局名字，以及加入进来的 skynet 节点的地址。本质上，这些地址也是一种名字。
// 同样可以用 key-value 的形式储存。即，每个 skynet 节点号对应一个字符串的地址。

// skynet的 master服务 master服务保存了key-value key就是skynet的handle value就handle对应的服务
// matser用于skynet不同节点间的同步

// 我设计了一台 master 中心服务器用来同步机器信息。把每个 skynet 进程上用于和其他机器通讯的部件称为 Harbor 。
// 每个 skynet 进程有一个 harbor id 为 1 到 255 （保留 0 给系统内部用）。在每个 skynet 进程启动时，
// 向 master 机器汇报自己的 harbor id 。一旦冲突，则禁止连入。

// master 服务其实就是一个简单的内存 key-value 数据库。数字 key 对应的 value 正是 harbor 的通讯地址。
// 另外，支持了拥有全局名字的服务，也依靠 master 机器同步。比如，你可以从某台 skynet 节点注册一个叫 DATABASE
// 的服务节点，它只要将 DATABASE 和节点 id 的对应关系通知 master 机器，就可以依靠 master 机器同步给所有注册入网络的 skynet 节点。

// master 做的事情很简单，其实就是回应名字的查询，以及在更新名字后，同步给网络中所有的机器。

// skynet 节点，通过 master ，认识网络中所有其它 skynet 节点。它们相互一一建立单向通讯通道。
// 也就是说，如果一共有 100 个 skynet 节点，在它们启动完毕后，会建立起 1 万条通讯通道。

// http://blog.codingnow.com/2012/08/skynet_harbor_rpc.html

#define HASH_SIZE 4096

// hash key-value表
struct name {
	struct name * next;
	char key[GLOBALNAME_LENGTH]; // GLOBALNAME_LENGTH: 16
	uint32_t hash;
	uint32_t value;
};

// hash name map
struct namemap {
	struct name *node[HASH_SIZE];
};

struct master {
	struct skynet_context *ctx;
	int remote_fd[REMOTE_MAX];      // REMOTE_MAX: 256 默认为256个节点 也可以更改 连接后返回的远程id 即对等方fd
	bool connected[REMOTE_MAX];     // 是否处于连接状态
	char * remote_addr[REMOTE_MAX]; // ip:port表 实际上我们把 ip:port有时也称为一个端点 endpoint
	struct namemap map;
};

// 注册skynet服务的标准接口
struct master *
master_create() {
	struct master *m = malloc(sizeof(*m));
	int i;
	for (i=0;i<REMOTE_MAX;i++) {
		m->remote_fd[i] = -1;
		m->remote_addr[i] = NULL;
		m->connected[i] = false;
	}
	memset(&m->map, 0, sizeof(m->map));
	return m;
}

void
master_release(struct master * m) {
	int i;

	struct skynet_context *ctx = m->ctx;

	for (i=0;i<REMOTE_MAX;i++) {
		int fd = m->remote_fd[i];
		if (fd >= 0) {
			assert(ctx);
			skynet_socket_close(ctx, fd); // 关闭socket fd
		}
		free(m->remote_addr[i]);
	}

	for (i=0;i<HASH_SIZE;i++) {
		struct name * node = m->map.node[i];
		while (node) {
			struct name * next = node->next;
			free(node);
			node = next;
		}
	}
	free(m);
}

// 在master中查找这个name对应的表节点
static struct name *
_search_name(struct master *m, char name[GLOBALNAME_LENGTH]) {
	uint32_t *ptr = (uint32_t *) name;
	uint32_t h = ptr[0] ^ ptr[1] ^ ptr[2] ^ ptr[3]; // 哈希key的计算
	struct name * node = m->map.node[h % HASH_SIZE];
	while (node) {
		if (node->hash == h && strncmp(node->key, name, GLOBALNAME_LENGTH) == 0) {
			return node;
		}
		node = node->next;
	}
	return NULL;
}

static struct name *
_insert_name(struct master *m, char name[GLOBALNAME_LENGTH]) {
	uint32_t *ptr = (uint32_t *)name;
	uint32_t h = ptr[0] ^ ptr[1] ^ ptr[2] ^ ptr[3];
	struct name **pname = &m->map.node[h % HASH_SIZE];
	struct name * node = malloc(sizeof(*node));
	memcpy(node->key, name, GLOBALNAME_LENGTH);
	node->next = *pname;
	node->hash = h;
	node->value = 0;
	*pname = node;
	return node;
}

static void
_copy_name(char *name, const char * buffer, size_t sz) {
	if (sz < GLOBALNAME_LENGTH) {
		memcpy(name, buffer, sz);
		memset(name+sz, 0 , GLOBALNAME_LENGTH - sz);
	} else {
		memcpy(name, buffer, GLOBALNAME_LENGTH);
	}
}

static void
_connect_to(struct master *m, int id) {
	assert(m->connected[id] == false);
	struct skynet_context * ctx = m->ctx;
	const char *ipaddress = m->remote_addr[id];
	char * portstr = strchr(ipaddress,':');
	if (portstr==NULL) {
		skynet_error(ctx, "Harbor %d : address invalid (%s)",id, ipaddress);
		return;
	}

	int sz = portstr - ipaddress;
	char tmp[sz + 1];
	memcpy(tmp,ipaddress,sz);
	tmp[sz] = '\0';
	int port = strtol(portstr+1,NULL,10);
	skynet_error(ctx, "Master connect to harbor(%d) %s:%d", id, tmp, port);
	m->remote_fd[id] = skynet_socket_connect(ctx, tmp, port); // socket connect return remote_fd[]
}

// 大小端字节序的转换
static inline void
to_bigendian(uint8_t *buffer, uint32_t n) {
	buffer[0] = (n >> 24) & 0xff;
	buffer[1] = (n >> 16) & 0xff;
	buffer[2] = (n >> 8) & 0xff;
	buffer[3] = n & 0xff;
}

// 发送消息
static void
_send_to(struct master *m, int id, const void * buf, int sz, uint32_t handle) {
	uint8_t * buffer= (uint8_t *)malloc(4 + sz + 12);
	to_bigendian(buffer, sz+12);
	memcpy(buffer+4, buf, sz);
	to_bigendian(buffer+4+sz, 0); // 转转成大端字节序 网络是大端字节序
	to_bigendian(buffer+4+sz+4, handle);
	to_bigendian(buffer+4+sz+8, 0);

	sz += 4 + 12;

	// skynet_socket_send send buffer to remote_fd
	if (skynet_socket_send(m->ctx, m->remote_fd[id], buffer, sz)) {
		skynet_error(m->ctx, "Harbor %d : send error", id);
	}
}

// 广播
static void
_broadcast(struct master *m, const char *name, size_t sz, uint32_t handle) {
	int i;
	for (i=1;i<REMOTE_MAX;i++) {
		int fd = m->remote_fd[i];
		if (fd < 0 || m->connected[i]==false)
			continue;
		_send_to(m, i , name, sz, handle); // send
	}
}

// 请求name
static void
_request_name(struct master *m, const char * buffer, size_t sz) {
	char name[GLOBALNAME_LENGTH];
	_copy_name(name, buffer, sz);
	struct name * n = _search_name(m, name);
	if (n == NULL) {
		return;
	}
	_broadcast(m, name, GLOBALNAME_LENGTH, n->value);
}

// 通知所有节点
static void
_update_name(struct master *m, uint32_t handle, const char * buffer, size_t sz) {
	char name[GLOBALNAME_LENGTH];
	_copy_name(name, buffer, sz);
	struct name * n = _search_name(m, name);
	if (n==NULL) {
		n = _insert_name(m,name); // 没有找到这个 name 在master中插入这个 Name
	}

	n->value = handle;
	_broadcast(m,name,GLOBALNAME_LENGTH, handle);
}

static void
close_harbor(struct master *m, int harbor_id) {
	if (m->connected[harbor_id]) {
		struct skynet_context * context = m->ctx;
		skynet_socket_close(context, m->remote_fd[harbor_id]);
		m->remote_fd[harbor_id] = -1;
		m->connected[harbor_id] = false;
	}
}

// 更新下地址
static void
_update_address(struct master *m, int harbor_id, const char * buffer, size_t sz) {
	if (m->remote_fd[harbor_id] >= 0) { // 这个remote_fd[harbor_id] 用过了 先清除下
		close_harbor(m, harbor_id);
	}

	free(m->remote_addr[harbor_id]);
	char * addr = malloc(sz+1);
	memcpy(addr, buffer, sz);
	addr[sz] = '\0';

	m->remote_addr[harbor_id] = addr;
	_connect_to(m, harbor_id); // conn fd
}

static int
socket_id(struct master *m, int id) {
	int i;
	for (i=1;i<REMOTE_MAX;i++) {
		if (m->remote_fd[i] == id)
			return i;
	}
	return 0;
}

static void
on_connected(struct master *m, int id) {
	_broadcast(m, m->remote_addr[id], strlen(m->remote_addr[id]), id);
	m->connected[id] = true;
	int i;
	for (i=1;i<REMOTE_MAX;i++) {
		if (i == id)
			continue;
		const char * addr = m->remote_addr[i];
		if (addr == NULL || m->connected[i] == false) {
			continue;
		}
		_send_to(m, id , addr, strlen(addr), i);
	}
}

static void
dispatch_socket(struct master *m, const struct skynet_socket_message *msg, int sz) {
	int id = socket_id(m, msg->id);
	switch(msg->type) {
	case SKYNET_SOCKET_TYPE_CONNECT:
		assert(id);
		on_connected(m, id);
		break;
	case SKYNET_SOCKET_TYPE_ERROR:
		skynet_error(m->ctx, "socket error on harbor %d", id);
		// go though, close socket
	case SKYNET_SOCKET_TYPE_CLOSE:
		close_harbor(m, id);
		break;
	default:
		skynet_error(m->ctx, "Invalid socket message type %d", msg->type);
		break;
	}
}


/*
	update global name to master

	4 bytes (handle) (handle == 0 for request)
	n bytes string (name)
 */

// 这个模块对应的主循环
static int
_mainloop(struct skynet_context * context, void * ud, int type, int session, uint32_t source, const void * msg, size_t sz) {
	if (type == PTYPE_SOCKET) { // skynet socket消息
		dispatch_socket(ud, msg, (int)sz);
		return 0;
	}

	// 别的为master服务
	if (type != PTYPE_HARBOR) {
		skynet_error(context, "None harbor message recv from %x (type = %d)", source, type);
		return 0;
	}

	assert(sz >= 4);
	struct master *m = ud;
	const uint8_t *handlen = msg;
	uint32_t handle = handlen[0]<<24 | handlen[1]<<16 | handlen[2]<<8 | handlen[3]; // 大小端字节序的转换
	sz -= 4;
	const char * name = msg;
	name += 4;

	// 同步不同的节点
	if (handle == 0) {
		_request_name(m , name, sz);
	}
	else if (handle < REMOTE_MAX) { // 没有达到最大数
		_update_address(m , handle, name, sz);
	}
	else {
		_update_name(m , handle, name, sz); // 插入新的name在master中
	}

	return 0;
}

// master服务初始化_init
int
master_init(struct master *m, struct skynet_context *ctx, const char * args) {
	char tmp[strlen(args) + 32];
	sprintf(tmp,"gate L ! %s %d %d 0",args,PTYPE_HARBOR,REMOTE_MAX);
	const char * gate_addr = skynet_command(ctx, "LAUNCH", tmp); // 返回的是16进制的handle
	// launch 简单的文本控制协议 得到 gate服务的addr 每一个服务实际上都是由服务+gate组成

	if (gate_addr == NULL) {
		skynet_error(ctx, "Master : launch gate failed");
		return 1;
	}

	// 得到 gate 的id
	uint32_t gate = strtoul(gate_addr+1, NULL, 16);
	if (gate == 0) {
		skynet_error(ctx, "Master : launch gate invalid %s", gate_addr);
		return 1;
	}

	// 得到自己的 addr REG命令 实际上市得到自己的handle
	const char * self_addr = skynet_command(ctx, "REG", NULL);
	int n = sprintf(tmp,"broker %s",self_addr);

	// skynet_send() 将消息发送出去
// skynet_send(struct skynet_context * context, uint32_t source, uint32_t destination ,
//													int type, int session, void * data, size_t sz)
	skynet_send(ctx, 0, gate, PTYPE_TEXT, 0, tmp, n);
	skynet_send(ctx, 0, gate, PTYPE_TEXT, 0, "start", 5);

	skynet_callback(ctx, m, _mainloop); // 设置这个ctx对应的回调函数

	m->ctx = ctx;
	return 0;
}
