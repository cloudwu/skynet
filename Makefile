PLAT ?= none
PLATS = linux freebsd macosx

CC ?= gcc

.PHONY : none $(PLATS) clean lua all

ifneq ($(PLAT), none)

.PHONY : default

default :
	$(MAKE) $(PLAT)

endif

none :
	@echo "Please do 'make PLATFORM' where PLATFORM is one of these:"
	@echo "   $(PLATS)"

LUA_STATICLIB = 3rd/lua/liblua.a
LUA_LIB ?= $(LUA_STATICLIB)
LUA_INC ?= -I3rd/lua
LUA_CLIB_PATH ?= luaclib
CSERVICE_PATH ?= cservice
SKYNET_BUILD_PATH ?= .


CFLAGS = -g -Wall $(LUA_INC) $(MYCFLAGS)

LIBS = -lpthread -lm
SHARED = -fPIC --shared
EXPORT = -Wl,-E

$(PLATS) : all 

linux : PLAT = linux
macosx : PLAT = macosx
freebsd : PLAT = freebsd

macosx linux : LIBS += -ldl
macosx : SHARED = -fPIC -dynamiclib -Wl,-undefined,dynamic_lookup
macosx : EXPORT =
linux freebsd : LIBS += -lrt

$(LUA_STATICLIB) :
	cd 3rd/lua && $(MAKE) CC=$(CC) $(PLAT)

CSERVICE = snlua logger gate client master multicast tunnel harbor localcast
LUA_CLIB = skynet socketdriver int64 mcast bson mongo md5

SKYNET_SRC = skynet_main.c skynet_handle.c skynet_module.c skynet_mq.c \
  skynet_server.c skynet_start.c skynet_timer.c skynet_error.c \
  skynet_harbor.c skynet_multicast.c skynet_group.c skynet_env.c \
  skynet_monitor.c skynet_socket.c socket_server.c

all : \
  $(SKYNET_BUILD_PATH)/skynet \
  $(foreach v, $(CSERVICE), $(CSERVICE_PATH)/$(v).so) \
  $(foreach v, $(LUA_CLIB), $(LUA_CLIB_PATH)/$(v).so) 

$(SKYNET_BUILD_PATH)/skynet : $(foreach v, $(SKYNET_SRC), skynet-src/$(v)) $(LUA_LIB)
	$(CC) $(CFLAGS) -o $@ $^ -Iskynet-src $(LDFLAGS) $(EXPORT) $(LIBS)

$(LUA_CLIB_PATH) :
	mkdir $(LUA_CLIB_PATH)

$(CSERVICE_PATH) :
	mkdir $(CSERVICE_PATH)

define CSERVICE_TEMP
  $$(CSERVICE_PATH)/$(1).so : service-src/service_$(1).c | $$(CSERVICE_PATH)
	$(CC) $$(CFLAGS) $$(SHARED) $$< -o $$@ -Iskynet-src
endef

$(foreach v, $(CSERVICE), $(eval $(call CSERVICE_TEMP,$(v))))

$(LUA_CLIB_PATH)/skynet.so : lualib-src/lua-skynet.c lualib-src/lua-seri.c lualib-src/trace_service.c lualib-src/timingqueue.c | $(LUA_CLIB_PATH)
	$(CC) $(CFLAGS) $(SHARED) $^ -o $@ -Iskynet-src -Iservice-src -Ilualib-src

$(LUA_CLIB_PATH)/socketdriver.so : lualib-src/lua-socket.c | $(LUA_CLIB_PATH)
	$(CC) $(CFLAGS) $(SHARED) $^ -o $@ -Iskynet-src -Iservice-src

$(LUA_CLIB_PATH)/int64.so : 3rd/lua-int64/int64.c | $(LUA_CLIB_PATH)
	$(CC) $(CFLAGS) $(SHARED) -O2 $^ -o $@ 

$(LUA_CLIB_PATH)/mcast.so : lualib-src/lua-localcast.c | $(LUA_CLIB_PATH)
	$(CC) $(CFLAGS) $(SHARED) $^ -o $@ -Iskynet-src -Iservice-src

$(LUA_CLIB_PATH)/bson.so : lualib-src/lua-bson.c | $(LUA_CLIB_PATH)
	$(CC) $(CFLAGS) $(SHARED) $^ -o $@ 

$(LUA_CLIB_PATH)/mongo.so : lualib-src/lua-mongo.c | $(LUA_CLIB_PATH)
	$(CC) $(CFLAGS) $(SHARED) $^ -o $@ 

$(LUA_CLIB_PATH)/md5.so : 3rd/lua-md5/md5.c 3rd/lua-md5/md5lib.c 3rd/lua-md5/compat-5.2.c | $(LUA_CLIB_PATH)
	$(CC) $(CFLAGS) $(SHARED) -O2 -I3rd/lua-md5 $^ -o $@ 

all : $(SKYNET_BUILD_PATH)/client

$(SKYNET_BUILD_PATH)/client : client-src/client.c
	$(CC) $(CFLAGS) $^ -o $@ -lpthread

clean :
	rm -f $(SKYNET_BUILD_PATH)/skynet $(SKYNET_BUILD_PATH)/client $(CSERVICE_PATH)/*.so $(LUA_CLIB_PATH)/*.so
