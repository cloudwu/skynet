#include "skynet_malloc.h"
#include "skynet_socket.h"

#include <lua.h>
#include <lauxlib.h>

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <sys/time.h>

#define QUEUESIZE 1024
#define HASHSIZE 4096
#define SMALLSTRING 2048
#define HEADERSIZE 1024
#define WEBSOCKET_HEADER_LEN  2
#define WEBSOCKET_MASK_LEN    4
#define MAX_PACKSIZE (10*1024)

#define TYPE_DATA 1
#define TYPE_MORE 2
#define TYPE_ERROR 3
#define TYPE_OPEN 4
#define TYPE_CLOSE 5
#define TYPE_WARNING 6

struct netpack {
	int id;
	int size;
	void * buffer;
};

struct uncomplete {
	struct netpack pack;
	struct uncomplete * next;
	uint8_t header[HEADERSIZE];
	int header_size;
	int read;
    // websocket mask
    int mask;
    int ismask;
    int hasunmask_size;
};

struct queue {
	int cap;
	int head;
	int tail;
	struct uncomplete * hash[HASHSIZE];
	struct netpack queue[QUEUESIZE];
};

/*
  打印二进制流(转换为十六进制字符串)
*/
static void print_bin(const char* pHead, const char *pBuffer, int iLength)
{
	FILE* fp = NULL;
    int i;
    char tmpBuffer[16384];
    char strTemp[32];

    if( iLength <= 0 || iLength > 4096 || pBuffer == NULL )
    {
        return;
    }

	fp = fopen("test.txt", "a");

    tmpBuffer[0] = '\0';
    for( i = 0; i < iLength; i++ )
    {
        if( !(i%16) )
        {
            sprintf(strTemp, "\n%04d>    ", i/16+1);
            strcat(tmpBuffer, strTemp);
        }
        sprintf(strTemp, "%02X ", (unsigned char)pBuffer[i]);
        strcat(tmpBuffer, strTemp);
    }

    strcat(tmpBuffer, "\n");
    fprintf(fp, "%s Size:%d Hex:%s\n", pHead, iLength, tmpBuffer);
    fclose(fp);    
    return;
}

static void
clear_list(struct uncomplete * uc) {
	while (uc) {
		skynet_free(uc->pack.buffer);
		void * tmp = uc;
		uc = uc->next;
		skynet_free(tmp);
	}
}

static int
lclear(lua_State *L) {
	struct queue * q = lua_touserdata(L, 1);
	if (q == NULL) {
		return 0;
	}
	int i;
	for (i=0;i<HASHSIZE;i++) {
		clear_list(q->hash[i]);
		q->hash[i] = NULL;
	}
	if (q->head > q->tail) {
		q->tail += q->cap;
	}
	for (i=q->head;i<q->tail;i++) {
		struct netpack *np = &q->queue[i % q->cap];
		skynet_free(np->buffer);
	}
	q->head = q->tail = 0;

	return 0;
}

static inline int
hash_fd(int fd) {
	int a = fd >> 24;
	int b = fd >> 12;
	int c = fd;
	return (int)(((uint32_t)(a + b + c)) % HASHSIZE);
}

static struct uncomplete *
find_uncomplete(struct queue *q, int fd) {
	if (q == NULL)
		return NULL;
	int h = hash_fd(fd);
	struct uncomplete * uc = q->hash[h];
	if (uc == NULL)
		return NULL;
	if (uc->pack.id == fd) {
		q->hash[h] = uc->next;
		return uc;
	}
	struct uncomplete * last = uc;
	while (last->next) {
		uc = last->next;
		if (uc->pack.id == fd) {
			last->next = uc->next;
			return uc;
		}
		last = uc;
	}
	return NULL;
}

static struct queue *
get_queue(lua_State *L) {
	struct queue *q = lua_touserdata(L,1);
	if (q == NULL) {
		q = lua_newuserdata(L, sizeof(struct queue));
		q->cap = QUEUESIZE;
		q->head = 0;
		q->tail = 0;
		int i;
		for (i=0;i<HASHSIZE;i++) {
			q->hash[i] = NULL;
		}
		lua_replace(L, 1);
	}
	return q;
}

static void
expand_queue(lua_State *L, struct queue *q) {
	struct queue *nq = lua_newuserdata(L, sizeof(struct queue) + q->cap * sizeof(struct netpack));
	nq->cap = q->cap + QUEUESIZE;
	nq->head = 0;
	nq->tail = q->cap;
	memcpy(nq->hash, q->hash, sizeof(nq->hash));
	memset(q->hash, 0, sizeof(q->hash));
	int i;
	for (i=0;i<q->cap;i++) {
		int idx = (q->head + i) % q->cap;
		nq->queue[i] = q->queue[idx];
	}
	q->head = q->tail = 0;
	lua_replace(L,1);
}

static void
push_data(lua_State *L, int fd, void *buffer, int size, int clone) {
	if (clone) {
		void * tmp = skynet_malloc(size);
		memcpy(tmp, buffer, size);
		buffer = tmp;
	}

	//print_bin("push_data", buffer, size);

	struct queue *q = get_queue(L);
	struct netpack *np = &q->queue[q->tail];
	if (++q->tail >= q->cap)
		q->tail -= q->cap;
	np->id = fd;
	np->buffer = buffer;
	np->size = size;
	if (q->head == q->tail) {
		expand_queue(L, q);
	}
}

static struct uncomplete * save_uncomplete(lua_State *L, int fd) {
	struct queue *q = get_queue(L);
	int h = hash_fd(fd);
	struct uncomplete * uc = skynet_malloc(sizeof(struct uncomplete));
	memset(uc, 0, sizeof(*uc));
	uc->next = q->hash[h];
	uc->pack.id = fd;
	q->hash[h] = uc;

	return uc;
}

static uint64_t ntoh64(uint64_t host) {
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
/*
* @return -1表示包头长不够 -2表示包前两个字节无效逻辑需要扔掉 -3表示是客户端浏览器关闭
*/
static inline int
read_size(uint8_t * buffer, int size, int* pack_head_length, int* mask, int * ismask, int * hasunmask_size) {
	
	if (size < 2) {
		return -1;
	}
	
	char fin = (buffer[0] >> 7) & 0x1;
    char rsv1 = (buffer[0] >> 6) & 0x1;
    char rsv2 = (buffer[0] >> 5) & 0x1;
    char rsv3 = (buffer[0] >> 4) & 0x1;
    char opcode = buffer[0] & 0xf;
    char is_mask = (buffer[1] >> 7) & 0x1;

    //printf("read_size2 fin=%d rsv1=%d rsv2=%d rsv3=%d opcode=%d is_mask=%d\n", fin, rsv1, rsv2, rsv3, opcode, is_mask);
    if (0x0 != rsv1 || 0x0 != rsv2 || 0x0 != rsv3) {
        return -2;
    }

    if (fin == 0 || opcode == 0) {
    	return -2;
    }

	if (opcode == 8) {
		return -3;
	}

	int offset = 0;
	int pack_size = 0;
    //0-125
    char length = buffer[1] & 0x7f;
    offset += WEBSOCKET_HEADER_LEN;
    //126
    if (length < 0x7E) {
        pack_size = length;
    }
    //Short
    else if (0x7E == length) {
		if (size < WEBSOCKET_HEADER_LEN + sizeof(short)) {
			return -1;
		}
        pack_size = ntohs(*((uint16_t *) (buffer+WEBSOCKET_HEADER_LEN)));
        //printf("read_size3 pack_size=%d sizeof(short)=%d sizeof(uint16_t)=%d\n", pack_size, sizeof(short), sizeof(uint16_t));
        offset += sizeof(short);
    }
    else {
		if (size < WEBSOCKET_HEADER_LEN + sizeof(int64_t)) {
			return -1;
		}
        pack_size = ntoh64(*((uint64_t *) (buffer+WEBSOCKET_HEADER_LEN)));
        //printf("read_size4 pack_size=%d sizeof(int64_t)=%d sizeof(uint64_t)=%d\n", pack_size, sizeof(int64_t), sizeof(uint64_t));
        offset += sizeof(int64_t);
    }
	
    if (is_mask) {
        if (offset + WEBSOCKET_MASK_LEN > size) {
            return -1;
        }

        *ismask = 1;

        char *masks = (char*)mask;
        memcpy(masks, (buffer + offset), WEBSOCKET_MASK_LEN);
        offset += WEBSOCKET_MASK_LEN;
    }

	*pack_head_length = offset;

	return pack_size;
}

static void decode_wsmask_data(uint8_t* buffer, int size, struct uncomplete *uc)
{
        if (uc == NULL)
        {
            return;
        }

        if (! uc->ismask ) 
        {
            return;
        }

        char *masks = (char*)(&(uc->mask));
        if (size)
        {
            int i;
            for (i = 0; i < size; i++)
            {
                buffer[i] ^= masks[(i+uc->hasunmask_size) % WEBSOCKET_MASK_LEN];
            }
            uc->hasunmask_size += size;
        }
}

static int websocket_strnpos(char *haystack, uint32_t haystack_length, char *needle, uint32_t needle_length)
{
    assert(needle_length > 0);
    uint32_t i;
    if (haystack_length < needle_length)
    {
        return -1;
    }
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
/*
* @param  buffer   数据buffer
* @param  size     数据buffer的大小
* @return  -1表示解析失败， >0表示解析成功返回header的长度
*/
static int get_http_header(uint8_t* buffer, int size)
{
	int n = websocket_strnpos((char*)buffer, size, "\r\n\r\n", 4);
	if (n < 0)
	{
		return n;
	}
	
	return (n+4);
}
static void
push_more(lua_State *L, int fd, uint8_t *buffer, int size, int wsocket_handeshake) {

	int pack_size = 0;
	int pack_head_length = 0;
    int mask = 0;
    int ismask = 0;
    int hasunmask_size = 0;
	if (wsocket_handeshake)
	{
		//认为socket初次建立连接读取握手协议
		pack_size = get_http_header(buffer, size);			
		//printf("push_more wsocket_handeshake=%d buffersize=%d pack_size=%d\n", wsocket_handeshake, size, pack_size);
	}
	else
	{
		//读取帧大小
		while ((pack_size = read_size(buffer, size, &pack_head_length, &mask, &ismask, &hasunmask_size)) <= -2)
		{
            mask = 0;
            ismask = 0;
            hasunmask_size = 0;
			buffer += WEBSOCKET_HEADER_LEN;
			size -= WEBSOCKET_HEADER_LEN;
		}
		if (pack_size > MAX_PACKSIZE) {
			pack_size = MAX_PACKSIZE;
		}
		//printf("push_more not wsocket_handeshake buffersize=%d pack_size=%d pack_head_length=%d mask=%d ismask=%d hasunmask_size=%d\n"
		//	, size, pack_size, pack_head_length, mask, ismask, hasunmask_size);
	}
	
	if (pack_size == -1)
	{			 
		struct uncomplete * uc = save_uncomplete(L, fd);
		uc->read = -1;

		if (wsocket_handeshake && size > HEADERSIZE) {
			uc->header_size = HEADERSIZE;
			memcpy(uc->header, buffer, HEADERSIZE);
		}
		else {
			uc->header_size += size;
			memcpy(uc->header, buffer, size);				
		}
		return;			
	}	

	buffer += pack_head_length;
	size -= pack_head_length;

	if (size < pack_size && !wsocket_handeshake) {
		struct uncomplete * uc = save_uncomplete(L, fd);
		uc->read = size;
        uc->mask = mask;
        uc->ismask = ismask;
        uc->hasunmask_size = hasunmask_size;
		uc->pack.size = pack_size;
		uc->pack.buffer = skynet_malloc(pack_size);
		decode_wsmask_data(buffer, uc->read, uc);
		memcpy(uc->pack.buffer, buffer, uc->read);
		return;
	}

	struct uncomplete uc;
	memset(&uc, 0, sizeof(uc));
	uc.mask = mask;
	uc.ismask = ismask;
	uc.hasunmask_size = hasunmask_size;
	decode_wsmask_data(buffer, pack_size, &uc);
	push_data(L, fd, buffer, pack_size, 1);

	buffer += pack_size;
	size -= pack_size;
	if (size > 0) {
		push_more(L, fd, buffer, size, wsocket_handeshake);
	}
}

static void
close_uncomplete(lua_State *L, int fd) {
	struct queue *q = lua_touserdata(L,1);
	struct uncomplete * uc = find_uncomplete(q, fd);
	if (uc) {
		skynet_free(uc->pack.buffer);
		skynet_free(uc);
	}
}

static int
filter_data_(lua_State *L, int fd, uint8_t * buffer, int size, int wsocket_handeshake) {
	struct queue *q = lua_touserdata(L,1);
	struct uncomplete * uc = find_uncomplete(q, fd);
    int pack_size = 0;
	int pack_head_length = 0;
    int mask = 0;
    int ismask = 0;
    int hasunmask_size = 0;
    static int total_size = 0;

    total_size += size;
    //print_bin("fileter", buffer, size);
    //printf("totalsize=%d filter_data size=%d wsocket_handeshake=%d\n", total_size, size, wsocket_handeshake);

	if (uc) {
		// fill uncomplete
		if (uc->read < 0) {
			//printf("uc->read < 0: buffersize=%d wsocket_handeshake=%d uc.header_size=%d\n", size, wsocket_handeshake, uc->header_size);
			// read size
			int index = 0;
			while (size > 0) {
				uc->header[uc->header_size] = buffer[index];
				index += 1;
				uc->header_size += 1;
				if (wsocket_handeshake) {
					//认为socket初次建立连接读取握手协议
					if (uc->header_size > HEADERSIZE) {
						uc->header_size = HEADERSIZE;
						pack_size = HEADERSIZE;
					}
					else {
						pack_size = get_http_header(uc->header, uc->header_size);						
					}
				}
				else {
					//读取帧大小
					while ((pack_size = read_size(uc->header, uc->header_size, &pack_head_length, &mask, &ismask, &hasunmask_size)) == -2) {
	                    mask  = 0;
	                    ismask = 0;
	                    hasunmask_size = 0;
						uc->header_size -= WEBSOCKET_HEADER_LEN;
						memmove(uc->header, uc->header + WEBSOCKET_HEADER_LEN, uc->header_size);
					}
					if (pack_size > MAX_PACKSIZE) {
						pack_size = MAX_PACKSIZE;
					}
				}

				if (pack_size >= 0 || index >= size) {
					//printf("uc->read < 0: pack_size=%d index=%d uc.header_size=%d mask=%d ismask=%d hasunmask_size=%d\n"
					//	, pack_size, index, uc->header_size, mask, ismask, hasunmask_size);
					size -= index;
					buffer += index;
					break;
				} 
			}
			
			if (pack_size == -1) {			
				int h = hash_fd(fd);
				uc->next = q->hash[h];
				q->hash[h] = uc;
				return 1;			
			} else if (pack_size == -3) {
				close_uncomplete(L, fd);
				lua_pushvalue(L, lua_upvalueindex(TYPE_CLOSE));
				lua_pushinteger(L, fd);
				return 3;
			}

			//取得包头长度以后开始生成新包
			uc->pack.buffer = skynet_malloc(pack_size);
			uc->pack.size = pack_size;
			uc->mask = mask;
            uc->ismask = ismask;
            uc->hasunmask_size = hasunmask_size;
            if (wsocket_handeshake) {
				uc->read = uc->header_size < pack_size ? uc->header_size : pack_size;
				memcpy(uc->pack.buffer, uc->header, uc->read);
            } 
            else {
            	uc->read = 0;
				/*
				uc->read = size < pack_size ? size : pack_size;
				decode_wsmask_data(buffer, uc->read, uc);
				memcpy(uc->pack.buffer, buffer, uc->read);
	            buffer += uc->read;
	            size -= uc->read;
	            */
            }
		}
		int need = uc->pack.size - uc->read;
		if (size < need) {
            decode_wsmask_data(buffer, size, uc);
			memcpy(uc->pack.buffer + uc->read, buffer, size);
			uc->read += size;
			int h = hash_fd(fd);
			uc->next = q->hash[h];
			q->hash[h] = uc;
			return 1;
		}

        decode_wsmask_data(buffer, need, uc);
		memcpy(uc->pack.buffer + uc->read, buffer, need);			
		
		buffer += need;
		size -= need;
		
		if (size == 0) {
			lua_pushvalue(L, lua_upvalueindex(TYPE_DATA));
			lua_pushinteger(L, fd);
			lua_pushlightuserdata(L, uc->pack.buffer);
			lua_pushinteger(L, uc->pack.size);
			//print_bin("push_data", uc->pack.buffer, uc->pack.size);
			skynet_free(uc);
			return 5;
		}
		// more data
		push_data(L, fd, uc->pack.buffer, uc->pack.size, 0);
		skynet_free(uc);
		push_more(L, fd, buffer, size, wsocket_handeshake);
		lua_pushvalue(L, lua_upvalueindex(TYPE_MORE));
		return 2;
	} else {
		if (wsocket_handeshake) {
			//认为socket初次建立连接读取握手协议
			pack_size = get_http_header(buffer, size);
			//printf("fileter wsocket_handeshake buffersize=%d pack_size=%d\n", size, pack_size);
			//printf("buffer:%s\n", buffer);			
		}
		else {
			//读取帧大小
			while ((pack_size = read_size(buffer, size, &pack_head_length, &mask, &ismask, &hasunmask_size)) == -2) {
                mask = 0;
                ismask = 0;
                hasunmask_size = 0;
				buffer += WEBSOCKET_HEADER_LEN;
				size -= WEBSOCKET_HEADER_LEN;
			}
			if (pack_size > MAX_PACKSIZE) {
				pack_size = MAX_PACKSIZE;
			}
			//printf("fileter not_handeshake buffersize=%d pack_size=%d pack_head_length=%d mask=%d ismask=%d hasunmask_size=%d\n"
			//	, size, pack_size, pack_head_length, mask, ismask, hasunmask_size);
		}
		
		if (pack_size == -1) {		
			struct uncomplete * uc = save_uncomplete(L, fd);
			uc->read = -1;
			if (wsocket_handeshake && size > HEADERSIZE) {
				uc->header_size = HEADERSIZE;
				memcpy(uc->header, buffer, HEADERSIZE);
			}
			else {
				uc->header_size += size;
				memcpy(uc->header, buffer, size);				
			}
			return 1;			
		} else if (pack_size == -3) {
			close_uncomplete(L, fd);
			lua_pushvalue(L, lua_upvalueindex(TYPE_CLOSE));
			lua_pushinteger(L, fd);
			return 3;
		}

		buffer+=pack_head_length;
		size-=pack_head_length;
		
		if (size < pack_size && !wsocket_handeshake) {
			struct uncomplete * uc = save_uncomplete(L, fd);
			uc->read = size;
            uc->mask = mask;
            uc->ismask = ismask;
            uc->hasunmask_size = hasunmask_size;
			uc->pack.size = pack_size;			
			uc->pack.buffer = skynet_malloc(pack_size);
			decode_wsmask_data(buffer, size, uc);
			memcpy(uc->pack.buffer, buffer, size);
			return 1;
		}

		struct uncomplete uc;
		memset(&uc, 0, sizeof(uc));
		uc.mask = mask;
		uc.ismask = ismask;
		uc.hasunmask_size = hasunmask_size;
		if (size == pack_size) {			
			// just one package
			lua_pushvalue(L, lua_upvalueindex(TYPE_DATA));
			lua_pushinteger(L, fd);
			void * result = skynet_malloc(pack_size);
			decode_wsmask_data(buffer, size, &uc);			
			memcpy(result, buffer, size);
			//print_bin("push_data", buffer, size);
			lua_pushlightuserdata(L, result);
			lua_pushinteger(L, size);
			return 5;
		}
		// more data
		decode_wsmask_data(buffer, pack_size, &uc);
		push_data(L, fd, buffer, pack_size, 1);
		buffer += pack_size;
		size -= pack_size;
		push_more(L, fd, buffer, size, wsocket_handeshake);
		lua_pushvalue(L, lua_upvalueindex(TYPE_MORE));
		return 2;
	}
}

static inline int
filter_data(lua_State *L, int fd, uint8_t * buffer, int size, int wsocket_handeshake) {
	int ret = filter_data_(L, fd, buffer, size, wsocket_handeshake);
	// buffer is the data of socket message, it malloc at socket_server.c : function forward_message .
	// it should be free before return,
	skynet_free(buffer);
	return ret;
}

static void
pushstring(lua_State *L, const char * msg, int size) {
	if (msg) {
		lua_pushlstring(L, msg, size);
	} else {
		lua_pushliteral(L, "");
	}
}

/*
	userdata queue
	lightuserdata msg
	integer size
	return
		userdata queue
		integer type
		integer fd
		string msg | lightuserdata/integer
 */
static int
lfilter(lua_State *L) {
	struct skynet_socket_message *message = lua_touserdata(L,2);
	int size = luaL_checkinteger(L,3);
	int wsocket_handeshake = luaL_checkinteger(L,4);
	char * buffer = message->buffer;
	if (buffer == NULL) {
		buffer = (char *)(message+1);
		size -= sizeof(*message);
	} else {
		size = -1;
	}

	lua_settop(L, 1);

	switch(message->type) {
	case SKYNET_SOCKET_TYPE_DATA:
		// ignore listen id (message->id)
		assert(size == -1);	// never padding string
		return filter_data(L, message->id, (uint8_t *)buffer, message->ud, wsocket_handeshake);
	case SKYNET_SOCKET_TYPE_CONNECT:
		// ignore listen fd connect
		return 1;
	case SKYNET_SOCKET_TYPE_CLOSE:
		// no more data in fd (message->id)
		close_uncomplete(L, message->id);
		lua_pushvalue(L, lua_upvalueindex(TYPE_CLOSE));
		lua_pushinteger(L, message->id);
		return 3;
	case SKYNET_SOCKET_TYPE_ACCEPT:
		lua_pushvalue(L, lua_upvalueindex(TYPE_OPEN));
		// ignore listen id (message->id);
		lua_pushinteger(L, message->ud);
		pushstring(L, buffer, size);
		return 4;
	case SKYNET_SOCKET_TYPE_ERROR:
		// no more data in fd (message->id)
		close_uncomplete(L, message->id);
		lua_pushvalue(L, lua_upvalueindex(TYPE_ERROR));
		lua_pushinteger(L, message->id);
		pushstring(L, buffer, size);
		return 4;
	case SKYNET_SOCKET_TYPE_WARNING:
		lua_pushvalue(L, lua_upvalueindex(TYPE_WARNING));
		lua_pushinteger(L, message->id);
		lua_pushinteger(L, message->ud);
		return 4;
	default:
		// never get here
		return 1;
	}
}

/*
	userdata queue
	return
		integer fd
		lightuserdata msg
		integer size
 */
static int
lpop(lua_State *L) {
	struct queue * q = lua_touserdata(L, 1);
	if (q == NULL || q->head == q->tail)
		return 0;
	struct netpack *np = &q->queue[q->head];
	if (++q->head >= q->cap) {
		q->head = 0;
	}

	lua_pushinteger(L, np->id);
	lua_pushlightuserdata(L, np->buffer);
	lua_pushinteger(L, np->size);

	return 3;
}

/*
	string msg | lightuserdata/integer

	lightuserdata/integer
 */

static const char *
tolstring(lua_State *L, size_t *sz, int index) {
	const char * ptr;
	if (lua_isuserdata(L,index)) {
		ptr = (const char *)lua_touserdata(L,index);
		*sz = (size_t)luaL_checkinteger(L, index+1);
	} else {
		ptr = luaL_checklstring(L, index, sz);
	}
	return ptr;
}

#define FRAME_SET_FIN(BYTE) (((BYTE) & 0x01) << 7)
#define FRAME_SET_OPCODE(BYTE) ((BYTE) & 0x0F)
#define FRAME_SET_MASK(BYTE) (((BYTE) & 0x01) << 7)
#define FRAME_SET_LENGTH(X64, IDX) (unsigned char)(((X64) >> ((IDX)*8)) & 0xFF)

static int
lpack(lua_State *L) {
	size_t len;
	const char * ptr = tolstring(L, &len, 1);

	int pos = 0;
    char frame_header[16];

    frame_header[pos++] = FRAME_SET_FIN(1) | FRAME_SET_OPCODE(2);
    if (len < 126)
    {
        frame_header[pos++] = FRAME_SET_MASK(0) | FRAME_SET_LENGTH(len, 0);
    }
    else
    {
        if (len < 65536)
        {
            frame_header[pos++] = FRAME_SET_MASK(0) | 126;
        }
        else
        {
            frame_header[pos++] = FRAME_SET_MASK(0) | 127;
            frame_header[pos++] = FRAME_SET_LENGTH(len, 7);
            frame_header[pos++] = FRAME_SET_LENGTH(len, 6);
            frame_header[pos++] = FRAME_SET_LENGTH(len, 5);
            frame_header[pos++] = FRAME_SET_LENGTH(len, 4);
            frame_header[pos++] = FRAME_SET_LENGTH(len, 3);
            frame_header[pos++] = FRAME_SET_LENGTH(len, 2);
        }
        frame_header[pos++] = FRAME_SET_LENGTH(len, 1);
        frame_header[pos++] = FRAME_SET_LENGTH(len, 0);
    }
		
	uint8_t * buffer = skynet_malloc(len + pos);
	memcpy(buffer, frame_header, pos);
	memcpy(buffer+pos, ptr, len);

	lua_pushlightuserdata(L, buffer);
	lua_pushinteger(L, len + pos);

	return 2;
}

static int
ltostring(lua_State *L) {
	void * ptr = lua_touserdata(L, 1);
	int size = luaL_checkinteger(L, 2);
	if (ptr == NULL) {
		lua_pushliteral(L, "");
	} else {
		lua_pushlstring(L, (const char *)ptr, size);
		skynet_free(ptr);
	}
	return 1;
}

static int 
lgetms(lua_State *L) {
	struct timeval tv;
    gettimeofday(&tv,NULL);
    long millisecond = (tv.tv_sec*1000000+tv.tv_usec)/1000;
    lua_pushnumber(L, millisecond);
    return 1;
}


int
luaopen_websocketnetpack(lua_State *L) {
	luaL_checkversion(L);
    luaL_Reg l[] = {
        { "pop", lpop },
        { "pack", lpack },
        { "clear", lclear },
        { "tostring", ltostring },
        { "getms", lgetms },
        { NULL, NULL },
    };
    luaL_newlib(L,l);

    // the order is same with macros : TYPE_* (defined top)
    lua_pushliteral(L, "data");
    lua_pushliteral(L, "more");
    lua_pushliteral(L, "error");
    lua_pushliteral(L, "open");
    lua_pushliteral(L, "close");
    lua_pushliteral(L, "warning");
    lua_pushcclosure(L, lfilter, 6);
    lua_setfield(L, -2, "filter");
    return 1;
}	
