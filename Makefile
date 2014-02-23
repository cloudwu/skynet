.PHONY : all clean 

CFLAGS = -g -Wall 
LDFLAGS = -lpthread -llua -lm

uname_S := $(shell sh -c 'uname -s 2>/dev/null || echo not')
ifeq ($(uname_S), Darwin)
	SHARED = -fPIC -dynamiclib -Wl,-undefined,dynamic_lookup
else
	LDFLAGS += -lrt -Wl,-E
	SHARED = -fPIC --shared
endif

ifneq ($(uname_S), FreeBSD)
	LDFLAGS += -ldl
endif

all : \
  skynet \
  service/snlua.so \
  service/logger.so \
  service/gate.so \
  service/client.so \
  service/master.so \
  service/multicast.so \
  service/tunnel.so \
  service/harbor.so \
  service/localcast.so \
  luaclib/skynet.so \
  luaclib/socketdriver.so \
  luaclib/int64.so \
  luaclib/mcast.so \
  luaclib/bson.so \
  luaclib/mongo.so \
  client

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
  skynet-src/skynet_env.c \
  skynet-src/skynet_monitor.c \
  skynet-src/skynet_socket.c \
  skynet-src/socket_server.c \
  luacompat/compat52.c
	gcc $(CFLAGS) -Iluacompat -o $@ $^ -Iskynet-src $(LDFLAGS)

luaclib:
	mkdir luaclib

service/tunnel.so : service-src/service_tunnel.c
	gcc $(CFLAGS) $(SHARED) $^ -o $@ -Iskynet-src

service/multicast.so : service-src/service_multicast.c
	gcc $(CFLAGS) $(SHARED) $^ -o $@ -Iskynet-src

service/master.so : service-src/service_master.c
	gcc $(CFLAGS) $(SHARED) $^ -o $@ -Iskynet-src

service/harbor.so : service-src/service_harbor.c
	gcc $(CFLAGS) $(SHARED) $^ -o $@ -Iskynet-src

service/logger.so : skynet-src/skynet_logger.c
	gcc $(CFLAGS) $(SHARED) $^ -o $@ -Iskynet-src

service/snlua.so : service-src/service_lua.c service-src/luacode_cache.c service-src/luaalloc.c
	gcc $(CFLAGS) $(SHARED) -Iluacompat $^ -o $@ -Iskynet-src

service/gate.so : service-src/service_gate.c
	gcc $(CFLAGS) $(SHARED) $^ -o $@ -Iskynet-src

service/localcast.so : service-src/service_localcast.c
	gcc $(CFLAGS) $(SHARED) $^ -o $@ -Iskynet-src

luaclib/skynet.so : lualib-src/lua-skynet.c lualib-src/lua-seri.c lualib-src/trace_service.c lualib-src/timingqueue.c | luaclib
	gcc $(CFLAGS) $(SHARED) -Iluacompat $^ -o $@ -Iskynet-src -Iservice-src -Ilualib-src

service/client.so : service-src/service_client.c
	gcc $(CFLAGS) $(SHARED) $^ -o $@ -Iskynet-src

luaclib/socketdriver.so : lualib-src/lua-socket.c | luaclib
	gcc $(CFLAGS) $(SHARED) -Iluacompat $^ -o $@ -Iskynet-src -Iservice-src

luaclib/int64.so : lua-int64/int64.c | luaclib
	gcc $(CFLAGS) $(SHARED) -Iluacompat -O2 $^ -o $@ 

luaclib/mcast.so : lualib-src/lua-localcast.c | luaclib
	gcc $(CFLAGS) $(SHARED) -Iluacompat $^ -o $@ -Iskynet-src -Iservice-src

luaclib/bson.so : lualib-src/lua-bson.c | luaclib
	gcc $(CFLAGS) $(SHARED) -Iluacompat $^ -o $@ 

luaclib/mongo.so : lualib-src/lua-mongo.c | luaclib
	gcc $(CFLAGS) $(SHARED) -Iluacompat $^ -o $@ 

client : client-src/client.c
	gcc $(CFLAGS) $^ -o $@ -lpthread

clean :
	rm skynet client service/*.so luaclib/*.so
	
