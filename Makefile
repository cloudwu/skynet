.PHONY : all clean 

CFLAGS = -g -Wall
SHARED = -fPIC --shared

all : \
  skynet \
  service/snlua.so \
  service/logger.so \
  lualib/skynet.so \
  service/gate.so \
  service/client.so \
  service/broker.so \
  service/connection.so \
  client \
  lualib/socket.so \
  lualib/lpeg.so \
  lualib/protobuf.so \
  lualib/int64.so \
  service/master.so \
  service/multicast.so \
  service/harbor.so

skynet : \
  skynet-src/skynet_main.c \
  skynet-src/skynet_handle.c \
  skynet-src/skynet_module.c \
  skynet-src/skynet_mq.c \
  skynet-src/skynet_server.c \
  skynet-src/skynet_start.c \
  skynet-src/skynet_timer.c \
  skynet-src/skynet_error.c \
  skynet-src/skynet_harbor.c \
  skynet-src/skynet_multicast.c \
  skynet-src/skynet_group.c \
  skynet-src/skynet_env.c
	gcc $(CFLAGS) -Wl,-E -o $@ $^ -Iskynet-src -lpthread -ldl -lrt -Wl,-E -llua -lm

service/multicast.so : service-src/service_multicast.c
	gcc $(CFLAGS) $(SHARED) $^ -o $@ -Iskynet-src

service/master.so : service-src/service_master.c
	gcc $(CFLAGS) $(SHARED) $^ -o $@ -Iskynet-src

service/harbor.so : service-src/service_harbor.c
	gcc $(CFLAGS) $(SHARED) $^ -o $@ -Iskynet-src

service/logger.so : skynet-src/skynet_logger.c
	gcc $(CFLAGS) $(SHARED) $^ -o $@ -Iskynet-src

service/snlua.so : service-src/service_lua.c
	gcc $(CFLAGS) $(SHARED) $^ -o $@ -Iskynet-src

service/gate.so : gate/mread.c gate/ringbuffer.c gate/main.c
	gcc $(CFLAGS) $(SHARED) $^ -o $@ -Igate -Iskynet-src

lualib/skynet.so : lualib-src/lua-skynet.c lualib-src/lua-seri.c lualib-src/lua-remoteobj.c
	gcc $(CFLAGS) $(SHARED) $^ -o $@ -Iskynet-src

service/client.so : service-src/service_client.c
	gcc $(CFLAGS) $(SHARED) $^ -o $@ -Iskynet-src

service/connection.so : connection/connection.c connection/main.c
	gcc $(CFLAGS) $(SHARED) $^ -o $@ -Iskynet-src -Iconnection

lualib/socket.so : connection/lua-socket.c
	gcc $(CFLAGS) $(SHARED) $^ -o $@ -Iskynet-src -Iconnection

service/broker.so : service-src/service_broker.c
	gcc $(CFLAGS) $(SHARED) $^ -o $@ -Iskynet-src

lualib/lpeg.so : lpeg/lpeg.c
	gcc $(CFLAGS) $(SHARED) -O2 $^ -o $@ -Ilpeg

PROTOBUFSRC = \
  lua-protobuf/context.c \
  lua-protobuf/varint.c \
  lua-protobuf/array.c \
  lua-protobuf/pattern.c \
  lua-protobuf/register.c \
  lua-protobuf/proto.c \
  lua-protobuf/map.c \
  lua-protobuf/alloc.c \
  lua-protobuf/rmessage.c \
  lua-protobuf/wmessage.c \
  lua-protobuf/bootstrap.c \
  lua-protobuf/stringpool.c \
  lua-protobuf/decode.c

lualib/protobuf.so : $(PROTOBUFSRC) lua-protobuf/pbc-lua.c
	gcc $(CFLAGS) $(SHARED) -O2 $^ -o $@ 

lualib/int64.so : lua-int64/int64.c
	gcc $(CFLAGS) $(SHARED) -O2 $^ -o $@ 

client : client-src/client.c
	gcc $(CFLAGS) $^ -o $@ -lpthread

clean :
	rm skynet client lualib/*.so service/*.so
	