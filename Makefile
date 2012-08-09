.PHONY : all clean install

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
lualib/lpeg.so \
lualib/protobuf.so \
lualib/int64.so \
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

lualib/lpeg.so : lpeg/lpeg.c
	gcc -Wall -O2 -fPIC --shared $^ -o $@ -Ilpeg

lualib/protobuf.so : \
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
lua-protobuf/decode.c \
lua-protobuf/pbc-lua.c 
	gcc -O2 -Wall --shared -fPIC -o $@ $^  

lualib/int64.so : lua-int64/int64.c
	gcc -O2 -Wall --shared -fPIC -o $@ $^  

client : client-src/client.c
	gcc -Wall -g $^ -o $@ -lpthread

skynet-master : master/master.c master/main.c
	gcc -g -Wall -Imaster -o $@ $^ -lzmq

clean :
	rm skynet client skynet-master lualib/*.so service/*.so


$(SKYNET_PATH) :
	mkdir $@

install-libs : | $(SKYNET_PATH)
	cp -R lualib $(SKYNET_PATH)

install-bins : | $(SKYNET_PATH)
	cp skynet $(SKYNET_PATH)

$(SKYNET_PATH)/service : | $(SKYNET_PATH)
	mkdir $@

install-services : | $(SKYNET_PATH)/service
	cp service/connection.so $(SKYNET_PATH)/service/
	cp service/logger.so $(SKYNET_PATH)/service/
	cp service/gate.so $(SKYNET_PATH)/service/
	cp service/broker.so $(SKYNET_PATH)/service/
	cp service/snlua.so $(SKYNET_PATH)/service/
	cp service/client.so $(SKYNET_PATH)/service/
	cp service/launcher.lua $(SKYNET_PATH)/service/
	cp service/redis-cli.lua $(SKYNET_PATH)/service/


install : install-libs install-bins install-services
	
	
	