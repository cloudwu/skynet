all : skynet snlua.so logger.so skynet.so gate.so client skynet-master

skynet : skynet_main.c skynet_handle.c skynet_module.c skynet_mq.c skynet_server.c skynet_start.c skynet_timer.c skynet_error.c skynet_harbor.c
	gcc -Wall -g -Wl,-E -o $@ $^ -lpthread -ldl -lrt -Wl,-E -llua -lm -lzmq

logger.so : skynet_logger.c
	gcc -Wall -g -fPIC --shared $^ -o $@

snlua.so : service_lua.c
	gcc -Wall -g -fPIC --shared $^ -o $@ 

gate.so : gate/mread.c gate/ringbuffer.c gate/map.c gate/main.c
	gcc -Wall -g -fPIC --shared -o $@ $^ -I. -Igate 

skynet.so : lua-skynet.c
	gcc -Wall -g -fPIC --shared $^ -o $@

client : client.c
	gcc -Wall -g $^ -o $@

skynet-master : master/master.c
	gcc -g -Wall -o $@ $^ -lzmq

clean :
	rm skynet client skynet-master *.so

