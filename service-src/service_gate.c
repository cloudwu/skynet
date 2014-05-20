#include "skynet.h"
#include "skynet_socket.h"
#include "databuffer.h"
#include "hashid.h"

#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdarg.h>

#define BACKLOG 32

// 每个master服务    实际上是   master服务 + gate服务总和
// 每个harbor服务　实际上是   harbor服务+gate服务总和
// gate与connection
// gate服务用与skynet对外的TCP通信 它将外部的消息格式转化成skynet内部的消息

#if 0

	Gate 和 Connection

	以上提到的都是 skynet 内部的协作机制。但一个完整的游戏服务器避免不必和外界通讯。
	外界通讯有两种，一是游戏客互端使用 TCP 连接接入 skynet 节点。如果你对游戏不关心，那换个角度看，
	如果你用 skynet 实现一个 web 服务器的话，游戏客户端就可以等价于一个浏览器请求。
	另一个是第三方的服务，比如数据库服务，它接受一个或多个 TCP 连接。你需要从 skynet 内部建立一个 TCP 连接出去使用。
	虽然，完全可以编写一个以 skynet 接口规范实现的数据库，那会更高效，但现实中恐怕很难做到。能做的仅仅是实现一个内存 cache 而已。
	（比如，我用了不到 10 行 lua 代码，实现了一个简单的 key-value 的建议内存数据库的范例）
	前者，我称为 gate 服务。它的特征是监听一个 TCP 端口，接受连入的 TCP 连接，并把连接上获得的数据转发到 skynet 内部。
	Gate 可以用来消除外部数据包和 skynet 内部消息包的不一致性。外部 TCP 流的分包问题，是 Gate 实现上的约定。我实现了一个 gate 服务，
	它按两字节的大头字节序来表示一个分包长度。这个模块基于我前段时间的一个子项目 。理论上我可以实现的更为通用，
	可以支持可配置的分包方案（Erlang 在这方面做的很全面）。但我更想保持代码的精简。
	固然，如果用 skynet 去实现一个通用的 web server ，这个 gate 就不太合适了。
	但重写一个定制的 Gate 服务并不困难。为 web server 定制一个 gate 甚至更简单，因为不再需要分包了。

	Gate 会接受外部连接，并把连接相关信息转发给另一个服务去处理。它自己不做数据处理是因为我们需要保持 gate 实现的简洁高效。
	C 语言足以胜任这项工作。而包处理工作则和业务逻辑精密相关，我们可以用 Lua 完成。

	外部信息分两类，一类是连接本身的接入和断开消息，另一类是连接上的数据包。一开始，Gate 无条件转发这两类消息到同一个处理服务。
	但对于连接数据包，添加一个包头无疑有性能上的开销。所以 Gate 还接收另一种工作模式：把每个不同连接上的数据包转发给不同的独立服务上。
	每个独立服务处理单一连接上的数据包。

	或者，我们也可以选择把不同连接上的数据包从控制信息包（建立/断开连接）中分离开，
	但不区分不同连接而转发给同一数据处理服务（对数据来源不敏感，只对数据内容敏感的场合）。

	这三种模式，我分别称为 watchdog 模式，由 gate 加上包头，同时处理控制信息和数据信息的所有数据；agent 模式，
	让每个 agent 处理独立连接；以及 broker 模式，由一个 broker 服务处理不同连接上的所有数据包。无论是哪种模式，
	控制信息都是交给 watchdog 去处理的，而数据包如果不发给 watchdog 而是发送给 agent 或 broker 的话，则不会有额外的数据头（也减少了数据拷贝）。
	识别这些包是从外部发送进来的方法是检查消息包的类型是否为 PTYPE_CLIENT 。当然，你也可以自己定制消息类型让 gate 通知你。

	Skynet 的基础服务中，关于集群间通讯的那部分，已经采用了 gate 模块作为实现的一部分。但是 gate 模块是一个纯粹的 skynet 服务组件，
	仅使用了 skynet 对外的 api ，而没有涉及 skynet 内部的任何细节。在 Harbor 模块使用 gate 时，启用的 broker 模块，且定制了消息包类型为 PTYPE_HARBOR 。

	在开源项目的示范代码中，我们还启动了一个简单的 gate 服务，以及对应的 watchdog 和 agent 。可以用附带的 client 程序连接上去，
	通过文本协议和 skynet 进行交流。agent 会转发所有的 client 输入给 skynet 内部的 simpledb 服务，simpledb 是一个简易的 key-value 内存数据库。
	这样，从 client 就可以做基本的数据库查询和更新操作了。

	注意，Gate 只负责读取外部数据，但不负责回写。也就是说，向这些连接发送数据不是它的职责范畴。
	作为示范，skynet 开源项目实现了一个简单的回写代理服务，叫做 service_client 。启动这个服务，启动时绑定一个 fd ，
	发送给这个服务的消息包，都会被加上两字节的长度包头，写给对应的 fd 。根据不同的分包协议，可以自己定制不同的 client 服务来解决向外部连接发送数据的模块。

#endif // 0

// 1.watchdog 模式，由 gate 加上包头，同时处理控制信息和数据信息的所有数据；
// 2.agent    模式，让每个 agent 处理独立连接；
// 3.broker   模式，由一个 broker 服务处理不同连接上的所有数据包。
// 无论是哪种模式，控制信息都是交给 watchdog 去处理的，而数据包如果不发给 watchdog 而是发送给 agent 或 broker 的话，
// 则不会有额外的数据头（也减少了数据拷贝）。识别这些包是从外部发送进来的方法是检查消息包的类型是否为 PTYPE_CLIENT 。当然，你也可以自己定制消息类型让 gate 通知你。

// connection结构保存了客户端的连接信息
struct connection {
	int 				id;					// skynet_socket id skynet应用层有一个socket池
	uint32_t 			agent;
	uint32_t 			client;
	char 				remote_name[32];
	struct databuffer 	buffer;
};

// 对外的 tcp连接
struct gate {
	struct skynet_context  *ctx;
	int 					listen_id; 		 // listen fd
	uint32_t 				watchdog;        // watchdog handle
	uint32_t 				broker;          // handle
	int 					client_tag;
	int 					header_size;
	int 					max_connection;
	struct 					hashid hash;
	struct 					connection *conn; // 客户端连接fd的保存

	// todo: save message pool ptr for release
	struct messagepool 		mp;
};

struct gate *
gate_create(void) {
	struct gate * g = malloc(sizeof(*g));
	memset(g,0,sizeof(*g));
	g->listen_id = -1;
	return g;
}

void
gate_release(struct gate *g) {
	int i;
	struct skynet_context *ctx = g->ctx;
	for (i=0;i<g->max_connection;i++) {
		struct connection *c = &g->conn[i];
		if (c->id >=0) {
			skynet_socket_close(ctx, c->id); // 主动关闭和客户端的连接
		}
	}

	if (g->listen_id >= 0) {
		skynet_socket_close(ctx, g->listen_id);
	}

	messagepool_free(&g->mp);
	hashid_clear(&g->hash);
	free(g->conn);
	free(g);
}

static void
_parm(char *msg, int sz, int command_sz) {
	while (command_sz < sz) {
		if (msg[command_sz] != ' ')
			break;
		++command_sz;
	}
	int i;
	for (i=command_sz;i<sz;i++) {
		msg[i-command_sz] = msg[i];
	}
	msg[i-command_sz] = '\0';
}

static void
_forward_agent(struct gate * g, int fd, uint32_t agentaddr, uint32_t clientaddr) {
	int id = hashid_lookup(&g->hash, fd);
	if (id >=0) {
		struct connection * agent = &g->conn[id];
		agent->agent = agentaddr;
		agent->client = clientaddr;
	}
}

// 控制命令的处理
static void
_ctrl(struct gate * g, const void * msg, int sz) {
	struct skynet_context * ctx = g->ctx;
	char tmp[sz+1];
	memcpy(tmp, msg, sz);
	tmp[sz] = '\0';
	char * command = tmp;

	int i;
	if (sz == 0)
		return;

	for (i=0;i<sz;i++) {
		if (command[i]==' ') {
			break;
		}
	}

	// kick 踢掉
	if (memcmp(command,"kick",i)==0) {
		_parm(tmp, sz, i);
		int uid = strtol(command , NULL, 10);
		int id = hashid_lookup(&g->hash, uid);
		if (id>=0) {
			skynet_socket_close(ctx, uid);
		}
		return;
	}

	// forward 向前传递消息
	if (memcmp(command,"forward",i)==0) {
		_parm(tmp, sz, i);
		char * client = tmp;
		char * idstr = strsep(&client, " ");
		if (client == NULL) {
			return;
		}
		int id = strtol(idstr , NULL, 10);
		char * agent = strsep(&client, " ");
		if (client == NULL) {
			return;
		}
		uint32_t agent_handle = strtoul(agent+1, NULL, 16);
		uint32_t client_handle = strtoul(client+1, NULL, 16);
		_forward_agent(g, id, agent_handle, client_handle);
		return;
	}

	// broker
	if (memcmp(command,"broker",i)==0) {
		_parm(tmp, sz, i);
		g->broker = skynet_queryname(ctx, command); // handle
		return;
	}

	// start
	if (memcmp(command,"start",i) == 0) {
		skynet_socket_start(ctx, g->listen_id);
		return;
	}

	// close
    if (memcmp(command, "close", i) == 0) {
		if (g->listen_id >= 0) {
			skynet_socket_close(ctx, g->listen_id);
			g->listen_id = -1;
		}
		return;
	}
	skynet_error(ctx, "[gate] Unkown command : %s", command);
}

// 报告？ 发送的是控制命令
static void
_report(struct gate * g, const char * data, ...) {
	if (g->watchdog == 0) {
		return;
	}

	struct skynet_context * ctx = g->ctx;
	va_list ap;
	va_start(ap, data);
	char tmp[1024];
	int n = vsnprintf(tmp, sizeof(tmp), data, ap);
	va_end(ap);

	skynet_send(ctx, 0, g->watchdog, PTYPE_TEXT,  0, tmp, n);
}

static void
_forward(struct gate *g, struct connection * c, int size) {
	struct skynet_context * ctx = g->ctx;
	if (g->broker) {
		void * temp = malloc(size);
		databuffer_read(&c->buffer,&g->mp,temp, size);
		skynet_send(ctx, 0, g->broker, g->client_tag | PTYPE_TAG_DONTCOPY, 0, temp, size);
		return;
	}
	if (c->agent) {
		void * temp = malloc(size);
		databuffer_read(&c->buffer,&g->mp,temp, size);
		skynet_send(ctx, c->client, c->agent, g->client_tag | PTYPE_TAG_DONTCOPY, 0 , temp, size);
	} else if (g->watchdog) {
		char * tmp = malloc(size + 32);
		int n = snprintf(tmp,32,"%d data ",c->id);
		databuffer_read(&c->buffer,&g->mp,tmp+n,size);
		skynet_send(ctx, 0, g->watchdog, PTYPE_TEXT | PTYPE_TAG_DONTCOPY, 0, tmp, size + n);
	}
}

// 分发消息
static void
dispatch_message(struct gate *g, struct connection *c, int id, void * data, int sz) {
	databuffer_push(&c->buffer,&g->mp, data, sz);
	for (;;) {
		int size = databuffer_readheader(&c->buffer, &g->mp, g->header_size);
		if (size < 0) {
			return;
		}
		else if (size > 0) {
			if (size >= 0x1000000) {
				struct skynet_context * ctx = g->ctx;
				databuffer_clear(&c->buffer,&g->mp);
				skynet_socket_close(ctx, id);
				skynet_error(ctx, "Recv socket message > 16M");
				return;
			}
			else {
				_forward(g, c, size);
				databuffer_reset(&c->buffer);
			}
		}
	}
}

// socket消息的处理
static void
dispatch_socket_message(struct gate *g, const struct skynet_socket_message * message, int sz) {
	struct skynet_context * ctx = g->ctx;
	switch(message->type) {

	// data
	case SKYNET_SOCKET_TYPE_DATA: {
		int id = hashid_lookup(&g->hash, message->id);
		if (id>=0) {
			struct connection *c = &g->conn[id];
			dispatch_message(g, c, message->id, message->buffer, message->ud);
		}
		else {
			skynet_error(ctx, "Drop unknown connection %d message", message->id);
			skynet_socket_close(ctx, message->id);
			free(message->buffer);
		}
		break;
	}

	// connect
	case SKYNET_SOCKET_TYPE_CONNECT: {
		if (message->id == g->listen_id) {
			// start listening
			break;
		}
		int id = hashid_lookup(&g->hash, message->id);
		if (id>=0) {
			struct connection *c = &g->conn[id];
			_report(g, "%d open %d %s:0",message->id,message->id,c->remote_name);
		}
		else {
			skynet_error(ctx, "Close unknown connection %d", message->id);
			skynet_socket_close(ctx, message->id);
		}
		break;
	}

	// close and error
	case SKYNET_SOCKET_TYPE_CLOSE:
	case SKYNET_SOCKET_TYPE_ERROR: {
		int id = hashid_remove(&g->hash, message->id);
		if (id>=0) {
			struct connection *c = &g->conn[id];
			databuffer_clear(&c->buffer,&g->mp);
			memset(c, 0, sizeof(*c));
			c->id = -1;
			_report(g, "%d close", message->id);
		}
		break;
	}

	// accept
	case SKYNET_SOCKET_TYPE_ACCEPT:
		// report accept, then it will be get a SKYNET_SOCKET_TYPE_CONNECT message
		assert(g->listen_id == message->id);
		if (hashid_full(&g->hash)) {
			skynet_socket_close(ctx, message->ud);
		} else {
			struct connection *c = &g->conn[hashid_insert(&g->hash, message->ud)];
			if (sz >= sizeof(c->remote_name)) {
				sz = sizeof(c->remote_name) - 1;
			}
			c->id = message->ud;
			memcpy(c->remote_name, message+1, sz);
			c->remote_name[sz] = '\0';
			skynet_socket_start(ctx, message->ud);
		}
		break;
	}
}

// gate module callback()
static int
_cb(struct skynet_context * ctx, void * ud, int type, int session, uint32_t source, const void * msg, size_t sz) {
	struct gate *g = ud;
	switch(type) {

	// skynet内部的文本协议 一般来说是控制命令
	case PTYPE_TEXT:
		_ctrl(g , msg , (int)sz);
		break;

	// 客户端的消息
	case PTYPE_CLIENT: {
		if (sz <=4 ) {
			skynet_error(ctx, "Invalid client message from %x",source);
			break;
		}

		// The last 4 bytes in msg are the id of socket, write following bytes to it
		// msg的后4个字节是socket的id 之后是剩下的字节
		const uint8_t * idbuf = msg + sz - 4;
		uint32_t uid = idbuf[0] | idbuf[1] << 8 | idbuf[2] << 16 | idbuf[3] << 24;

		// 找到这个socket id即在应用层维护的socket fd
		int id = hashid_lookup(&g->hash, uid);
		if (id>=0) {
			// don't send id (last 4 bytes)
			skynet_socket_send(ctx, uid, (void*)msg, sz-4);

			// return 1 means don't free msg
			return 1;
		}
		else {
			skynet_error(ctx, "Invalid client id %d from %x",(int)uid,source);
			break;
		}
	}

	// socket的消息类型 分发消息
	case PTYPE_SOCKET:
		assert(source == 0);
		// recv socket message from skynet_socket
		dispatch_socket_message(g, msg, (int)(sz-sizeof(struct skynet_socket_message)));
		break;
	}
	return 0;
}

static int
start_listen(struct gate *g, char * listen_addr) {
	struct skynet_context * ctx = g->ctx;
	char * portstr = strchr(listen_addr,':');
	const char * host = "";
	int port;
	if (portstr == NULL) {
		port = strtol(listen_addr, NULL, 10);
		if (port <= 0) {
			skynet_error(ctx, "Invalid gate address %s",listen_addr);
			return 1;
		}
	} else {
		port = strtol(portstr + 1, NULL, 10);
		if (port <= 0) {
			skynet_error(ctx, "Invalid gate address %s",listen_addr);
			return 1;
		}
		portstr[0] = '\0';
		host = listen_addr;
	}

	// socket_listen() to client connect
	g->listen_id = skynet_socket_listen(ctx, host, port, BACKLOG);
	if (g->listen_id < 0) {
		return 1;
	}
	return 0;
}

// gate_init()
int
gate_init(struct gate *g , struct skynet_context * ctx, char * parm) {
	if (parm == NULL)
		return 1;

	int max = 0; // max connext num
	int buffer = 0;
	int sz = strlen(parm)+1;

	char watchdog[sz];
	char binding[sz]; // binding: listen_addr

	int client_tag = 0;
	char header;

	int n = sscanf(parm, "%c %s %s %d %d %d",&header,watchdog, binding,&client_tag , &max,&buffer);
	if (n<4) {
		skynet_error(ctx, "Invalid gate parm %s",parm);
		return 1;
	}

	if (max <=0 ) {
		skynet_error(ctx, "Need max connection");
		return 1;
	}

	// header S或者L开头的
	if (header != 'S' && header !='L') {
		skynet_error(ctx, "Invalid data header style");
		return 1;
	}

	// PTYPE_CLIENT 客户端消息
	if (client_tag == 0) {
		client_tag = PTYPE_CLIENT;
	}

	if (watchdog[0] == '!') {
		g->watchdog = 0;
	}
	else {
		g->watchdog = skynet_queryname(ctx, watchdog);
		if (g->watchdog == 0) {
			skynet_error(ctx, "Invalid watchdog %s",watchdog);
			return 1;
		}
	}

	g->ctx = ctx;

	hashid_init(&g->hash, max); // hashid_intit()

	g->conn = malloc(max * sizeof(struct connection));
	memset(g->conn, 0, max *sizeof(struct connection));
	g->max_connection = max;

	int i;
	for (i=0;i<max;i++) {
		g->conn[i].id = -1;
	}
	
	g->client_tag = client_tag;
	g->header_size = (header=='S') ? 2 : 4;

	skynet_callback(ctx,g,_cb); // 设置这个模块的回调函数

	return start_listen(g, binding); // 开始监听客户端的连接 binding:listen_addr
}
