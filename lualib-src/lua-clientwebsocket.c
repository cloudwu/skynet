// simple lua socket library for client
// It's only for demo, limited feature. Don't use it in your project.
// Rewrite socket library by yourself .

#include <lua.h>
#include <lauxlib.h>
#include <string.h>
#include <stdint.h>
#include <pthread.h>
#include <stdlib.h>

#include <netinet/in.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/time.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>

#define WEBSOCKET_HEADER_LEN  2
#define WEBSOCKET_MASK_LEN    4
#define FRAME_SET_FIN(BYTE) (((BYTE) & 0x01) << 7)
#define FRAME_SET_OPCODE(BYTE) ((BYTE) & 0x0F)
#define FRAME_SET_MASK(BYTE) (((BYTE) & 0x01) << 7)
#define FRAME_SET_LENGTH(X64, IDX) (unsigned char)(((X64) >> ((IDX)*8)) & 0xFF)

#define CACHE_SIZE 0x1000	

#define WEBSOCKET_CLIENT_HEADER(ip, port) "GET / HTTP/1.1\r\n"  \
										  "Origin:\r\n"	\
										  "Host: "#ip":"#port"\r\n"	\
										  "Sec-WebSocket-Key: Vld9DdQJkJTVKdwRExlMA==\r\n"	\
										  "User-Agent: LuaWebSocketClient/1.0\r\n"	\
										  "Upgrade: Websocket\r\n"	\
										  "Connection: Upgrade\r\n"	\
										  "Sec-WebSocket-Protocol: wamp\r\n" \
										  "Sec-WebSocket-Version: 13\r\n"	\
										  "\r\n"  

static uint64_t ntoh64(uint64_t host)
{
    uint64_t ret = 0;
    uint32_t high, low;
    low = host & 0xFFFFFFFF;
    high = (host >> 32) & 0xFFFFFFFF;
    low = ntohl(low);
    high = ntohl(high);
    ret = low;
    ret <<= 32;
    ret |= high;
    return ret;
}

static int websocket_strnpos(char *haystack, uint32_t haystack_length, char *needle, uint32_t needle_length)
{
    if (needle_length <= 0) return -1;
    uint32_t i;

    for (i = 0; i < (int) (haystack_length - needle_length + 1); i++)
    {
        if ((haystack[0] == needle[0]) && (0 == memcmp(haystack, needle, needle_length)))
        {
            return i;
        }
        haystack++;
    }

    return -1;
}

static int get_http_header(char* buffer, int size)
{
	int n = websocket_strnpos((char*)buffer, size, "\r\n\r\n", 4);
	if (n < 0)
	{
		return n;
	}
	
	return (n+4);
}

static int recv_websocket_header_response(int fd)
{
	char buffer[CACHE_SIZE];
	int read_size = 0;
	int is_read_allheader = -1;

	while (1)
	{
		int r = recv(fd, buffer+read_size, CACHE_SIZE, 0);
		if (r == 0) {
			// close
			return -1;
		}

		if (r < 0) {
			if (errno == EAGAIN || errno == EINTR) {
				if (is_read_allheader > 0)
				{
					break;
				}
				usleep(1000);
				continue;
			}
			else
			{				
				return -1;
			}
		}

		read_size += r;
		is_read_allheader = get_http_header(buffer, read_size);
		if (is_read_allheader > 0)	break; 		
	}

	//检查websocket相应是否成功
	//待开发

	return 0;
} 

static int common_block_send(int fd, const char * buffer, int sz)
{
	while(sz > 0) {
		int r = send(fd, buffer, sz, 0);
		if (r < 0) {
			if (errno == EAGAIN || errno == EINTR)
				continue;
			return -1;
		}
		buffer += r;
		sz -= r;
	}

	return 0;	
} 

static int
lconnect(lua_State *L) {
	const char * addr = luaL_checkstring(L, 1);
	int port = luaL_checkinteger(L, 2);
	int fd = socket(AF_INET,SOCK_STREAM,0);
	struct sockaddr_in my_addr;

	my_addr.sin_addr.s_addr=inet_addr(addr);
	my_addr.sin_family=AF_INET;
	my_addr.sin_port=htons(port);

	int r = connect(fd,(struct sockaddr *)&my_addr,sizeof(struct sockaddr_in));

	if (r == -1) {
		return luaL_error(L, "Connect %s %d failed", addr, port);
	}

	int flag = fcntl(fd, F_GETFL, 0);
	fcntl(fd, F_SETFL, flag | O_NONBLOCK);

	//socket准备好，进行websocket握手
	char* websocket_header = WEBSOCKET_CLIENT_HEADER(addr, port);
	if (common_block_send(fd, websocket_header, strlen(websocket_header)) < 0)
	{
		return luaL_error(L, "Connect %s %d send websocket data failed", addr, port);		
	}

	if (recv_websocket_header_response(fd) < 0)
	{
		return luaL_error(L, "Connect %s %d websocket handleshake failed", addr, port);
	}

	lua_pushinteger(L, fd);

	return 1;
}

static int
lclose(lua_State *L) {
	int fd = luaL_checkinteger(L, 1);
	close(fd);

	return 0;
}

static void
websocket_block_send(lua_State *L, int fd, const char * buffer, int64_t sz) {
	char cache_buffer[CACHE_SIZE];
	int cache_size = 0;
	int pos = 0;
    char frame_header[16];

    frame_header[pos++] = FRAME_SET_FIN(1) | FRAME_SET_OPCODE(4);
    if (sz < 126)
    {
        frame_header[pos++] = FRAME_SET_MASK(0) | FRAME_SET_LENGTH(sz, 0);
    }
    else
    {
        if (sz < 65536)
        {
            frame_header[pos++] = FRAME_SET_MASK(0) | 126;
        }
        else
        {
            frame_header[pos++] = FRAME_SET_MASK(0) | 127;
            frame_header[pos++] = FRAME_SET_LENGTH(sz, 7);
            frame_header[pos++] = FRAME_SET_LENGTH(sz, 6);
            frame_header[pos++] = FRAME_SET_LENGTH(sz, 5);
            frame_header[pos++] = FRAME_SET_LENGTH(sz, 4);
            frame_header[pos++] = FRAME_SET_LENGTH(sz, 3);
            frame_header[pos++] = FRAME_SET_LENGTH(sz, 2);
        }
        frame_header[pos++] = FRAME_SET_LENGTH(sz, 1);
        frame_header[pos++] = FRAME_SET_LENGTH(sz, 0);
    }
		
	memcpy(cache_buffer, frame_header, pos);
	memcpy(cache_buffer+pos, buffer, sz);
	cache_size = sz + pos;

	common_block_send(fd, cache_buffer, cache_size);
}

static void
block_send(lua_State *L, int fd, const char * buffer, int sz) {
	while(sz > 0) {
		int r = send(fd, buffer, sz, 0);
		if (r < 0) {
			if (errno == EAGAIN || errno == EINTR)
				continue;
			luaL_error(L, "socket error: %s", strerror(errno));
		}
		buffer += r;
		sz -= r;
	}
}


static int
lwrite(lua_State *L) {
	int fd = luaL_checkinteger(L,1);
	void * buffer = lua_touserdata(L,2);
	int sz = luaL_checkinteger(L,3);

	websocket_block_send(L, fd, buffer, sz);

	return 0;
}

/*
	integer fd
	string message
 */
static int
lsend(lua_State *L) {
	size_t sz = 0;
	int fd = luaL_checkinteger(L,1);
	const char * msg = luaL_checklstring(L, 2, &sz);

	//block_send(L, fd, msg, (int)sz);
	websocket_block_send(L, fd, msg, (int)sz);
	return 0;
}

/*
	intger fd
	string last
	table result

	return 
		boolean (true: data, false: block, nil: close)
		string last
 */
struct socket_buffer {
	void * buffer;
	int sz;
};

/*
	return -3: 表示socket异常 -2:表示socket 超时 -1:表示socket关闭 0:表示读取成功
*/
static int block_read(int fd, char* buffer, int sz, int time_out)
{
    fd_set fds;
	struct timeval timeout;
    timeout.tv_sec = time_out / (1000*1000);
    timeout.tv_usec =  time_out % (1000*1000);

    while (sz)
    {
    	FD_ZERO(&fds);
    	FD_SET(fd, &fds);
    	switch(select(fd+1,&fds, NULL, NULL,&timeout))
    	{
    		case -1:
    		{
    			//printf("1-socket error -3\n");
    			return -3;
    		}
    		case 0:
    		{
    			//printf("1-socket error -2\n");
    			return -2;
    		}
    		default:
    		{
    			//内容可读
				int r = recv(fd, buffer, sz, 0);
				if (r == 0) {
					// close
					//printf("1-socket error -1\n");
					return -1;
				}
				if (r < 0) {
					if (errno == EAGAIN || errno == EINTR) {
						continue;
					}
    				//printf("1-socket error -3\n");
					return -3;
				}

				sz -= r;
    		}
    	}   	
    }

    return 0;
}

static int
lrecv(lua_State *L) {
	int fd = luaL_checkinteger(L,1);
	int time_out = luaL_checkinteger(L, 2);		//单位是us

    char buffer[CACHE_SIZE];
    char masks[WEBSOCKET_MASK_LEN];
    int data_len = 0;

    int res = 0;

    while (1)
    {

		//return -3: 表示socket异常 -2:表示socket 超时 -1:表示socket关闭 0:表示读取成功
	    //读 WEBSOCKET_HEADER_LEN
	    res =  block_read(fd, buffer, WEBSOCKET_HEADER_LEN, time_out);
	    printf("ws head: %hhx %hhx \n", buffer[0], buffer[1]);
	    if (res == -3)
	    {
	    	return luaL_error(L, "socket error: %s", strerror(errno));
	    }

	    if (res == -2)
	    {
		    lua_pushliteral(L, "");
		    lua_pushinteger(L, 1);
            return 2;
	    }

	    if (res == -1)
	    {
	    	lua_pushliteral(L, "");
			return 1;
	    }

		//char fin = (buffer[0] >> 7) & 0x1;
	    char rsv1 = (buffer[0] >> 6) & 0x1;
	    char rsv2 = (buffer[0] >> 5) & 0x1;
	    char rsv3 = (buffer[0] >> 4) & 0x1;
	    //char opcode = buffer[0] & 0xf;
	    char is_mask = (buffer[1] >> 7) & 0x1;
	    if (0x0 != rsv1 || 0x0 != rsv2 || 0x0 != rsv3)
    	{
        	continue;
    	}

	    //0-125
	    char length = buffer[1] & 0x7f;
	    //126
	    if (length < 0x7E)
	    {
	        data_len = length;
	    }
	    //Short
	    else if (0x7E == length)
	    {
			//读 data_len
		    res =  block_read(fd, buffer, sizeof(short), time_out);
		    if (res == -3)
		    {
		    	return luaL_error(L, "socket error: %s", strerror(errno));
		    }

		    if (res == -2)
		    {
		        lua_pushliteral(L, "");
		        lua_pushinteger(L, 1);
                return 2;
		    }

		    if (res == -1)
		    {
		    	lua_pushliteral(L, "");
				return 1;
		    }

	        data_len = ntohs(*((uint16_t *) buffer));
	    }
	    else
	    {
	    	//读 data_len
		    res =  block_read(fd, buffer, sizeof(int64_t), time_out);
		    if (res == -3)
		    {
		    	return luaL_error(L, "socket error: %s", strerror(errno));
		    }

		    if (res == -2)
		    {
		        lua_pushliteral(L, "");
		        lua_pushinteger(L, 1);
                return 2;
		    }

		    if (res == -1)
		    {
		    	lua_pushliteral(L, "");
				return 1;
		    }

	        data_len = ntoh64(*((uint64_t *) buffer));
	    }

	    if (is_mask)
	    {
	    	//读 mask
		    res =  block_read(fd, masks, WEBSOCKET_MASK_LEN, time_out);
		    if (res == -3)
		    {
		    	return luaL_error(L, "socket error: %s", strerror(errno));
		    }

		    if (res == -2)
		    {
		    	lua_pushliteral(L, "");
		    	lua_pushinteger(L, 1);
                return 2;
		    }

		    if (res == -1)
		    {
		    	lua_pushliteral(L, "");
				return 1;
		    }	    	
	    }

    	//读 data
	    res =  block_read(fd, buffer, data_len, time_out);
	    //printf("res=%d  fd =%d data_len=%d time_out=%d\n", res, fd, data_len, time_out);
	    if (res == -3)
	    {
	    	return luaL_error(L, "socket error: %s", strerror(errno));
	    }

	    if (res == -2)
	    {
		    lua_pushliteral(L, "");
		    lua_pushinteger(L, 1);
            return 2;
	    }

	    if (res == -1)
	    {
	    	lua_pushliteral(L, "");
			return 1;
	    }

	    if (is_mask)
	    {
			if (data_len)
	        {
	            int i;
	            for (i = 0; i < data_len; i++)
	            {
	                buffer[i] ^= masks[i % WEBSOCKET_MASK_LEN];
	            }
	        }
	    }

	    break;
    }

	lua_pushlstring(L, buffer, data_len);
	return 1;
}


static int
lusleep(lua_State *L) {
	int n = luaL_checknumber(L, 1);
	usleep(n);
	return 0;
}

// quick and dirty none block stdin readline

#define QUEUE_SIZE 1024

struct queue {
	pthread_mutex_t lock;
	int head;
	int tail;
	char * queue[QUEUE_SIZE];
};

static void *
readline_stdin(void * arg) {
	struct queue * q = arg;
	char tmp[1024];
	while (!feof(stdin)) {
		if (fgets(tmp,sizeof(tmp),stdin) == NULL) {
			// read stdin failed
			exit(1);
		}
		int n = strlen(tmp) -1;

		char * str = malloc(n+1);
		memcpy(str, tmp, n);
		str[n] = 0;

		pthread_mutex_lock(&q->lock);
		q->queue[q->tail] = str;

		if (++q->tail >= QUEUE_SIZE) {
			q->tail = 0;
		}
		if (q->head == q->tail) {
			// queue overflow
			exit(1);
		}
		pthread_mutex_unlock(&q->lock);
	}
	return NULL;
}

static int
lreadstdin(lua_State *L) {
	struct queue *q = lua_touserdata(L, lua_upvalueindex(1));
	pthread_mutex_lock(&q->lock);
	if (q->head == q->tail) {
		pthread_mutex_unlock(&q->lock);
		return 0;
	}
	char * str = q->queue[q->head];
	if (++q->head >= QUEUE_SIZE) {
		q->head = 0;
	}
	pthread_mutex_unlock(&q->lock);
	lua_pushstring(L, str);
	free(str);
	return 1;
}

static uint64_t
gettime() {
	uint64_t t;
	struct timeval tv;
	gettimeofday(&tv, NULL);
	t = (uint64_t)tv.tv_sec * 1000;
	t += tv.tv_usec / 1000;
	return t;
}

static int 
ltick(lua_State *L) {
	lua_pushinteger(L,gettime());
	return 1;
}

int
luaopen_clientwebsocket(lua_State *L) {
	luaL_checkversion(L);
	luaL_Reg l[] = {
		{ "connect", lconnect },
		{ "recv", lrecv },
		{ "send", lsend },
		{ "close", lclose },
		{ "usleep", lusleep },
		{ "tick", ltick },
		{ "write", lwrite },
		{ NULL, NULL },
	};
	luaL_newlib(L, l);

	struct queue * q = lua_newuserdata(L, sizeof(*q));
	memset(q, 0, sizeof(*q));
	pthread_mutex_init(&q->lock, NULL);
	lua_pushcclosure(L, lreadstdin, 1);
	lua_setfield(L, -2, "readstdin");

	pthread_t pid ;
	pthread_create(&pid, NULL, readline_stdin, q);

	return 1;
}
