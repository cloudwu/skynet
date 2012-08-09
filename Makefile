all : \
skynet \
service/snlua.so \
service/logger.so \
lualib/skynet.so \
service/gate.so \
service/client.so \
service/connection.so \
service/broker.so \
client \
skynet-master

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
skynet-src/skynet_env.c \
master/master.c 
	gcc -Wall -g -Wl,-E -o $@ $^ -Iskynet-src -Imaster -lpthread -ldl -lrt -Wl,-E -llua -lm -lzmq

service/logger.so : skynet-src/skynet_logger.c
	gcc -Wall -g -fPIC --shared $^ -o $@ -Iskynet-src

service/snlua.so : service-src/service_lua.c
	gcc -Wall -g -fPIC --shared $^ -o $@ -Iskynet-src

service/gate.so : gate/mread.c gate/ringbuffer.c gate/main.c
	gcc -Wall -g -fPIC --shared -o $@ $^ -I. -Igate -Iskynet-src

lualib/skynet.so : lualib-src/lua-skynet.c lua-serialize/serialize.c
	gcc -Wall -g -fPIC --shared $^ -o $@ -Iskynet-src -Ilua-serialize

service/client.so : service-src/service_client.c
	gcc -Wall -g -fPIC --shared $^ -o $@ -Iskynet-src

service/connection.so : connection/connection.c connection/main.c
	gcc -Wall -g -fPIC --shared -o $@ $^ -I. -Iconnection -Iskynet-src

service/broker.so : service-src/service_broker.c
	gcc -Wall -g -fPIC --shared $^ -o $@ -Iskynet-src

client : client-src/client.c
	gcc -Wall -g $^ -o $@ -lpthread

skynet-master : master/master.c master/main.c
	gcc -g -Wall -Imaster -o $@ $^ -lzmq

clean :
	rm skynet client skynet-master lualib/*.so service/*.so

