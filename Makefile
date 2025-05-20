include platform.mk

LUA_CLIB_PATH ?= luaclib
CSERVICE_PATH ?= cservice

SKYNET_BUILD_PATH ?= .

CFLAGS = -g -O2 -Wall -MMD -MP -I$(LUA_INC) $(MYCFLAGS)
# CFLAGS += -DUSE_PTHREAD_LOCK

# lua

LUA_STATICLIB := 3rd/lua/liblua.a
LUA_LIB ?= $(LUA_STATICLIB)
LUA_INC ?= 3rd/lua

$(LUA_STATICLIB) :
	$(MAKE) -C 3rd/lua CC='$(CC) -std=gnu99' $(PLAT)

# https : turn on TLS_MODULE to add https support

# TLS_MODULE=ltls
TLS_LIB=
TLS_INC=

# jemalloc

JEMALLOC_STATICLIB := 3rd/jemalloc/lib/libjemalloc_pic.a
JEMALLOC_INC := 3rd/jemalloc/include/jemalloc

all : jemalloc

.PHONY : jemalloc update3rd

MALLOC_STATICLIB := $(JEMALLOC_STATICLIB)

$(JEMALLOC_STATICLIB) : 3rd/jemalloc/Makefile
	$(MAKE) -C 3rd/jemalloc CC=$(CC)

3rd/jemalloc/autogen.sh :
	git submodule update --init

3rd/jemalloc/Makefile : | 3rd/jemalloc/autogen.sh
	cd 3rd/jemalloc && ./autogen.sh --with-jemalloc-prefix=je_ --enable-prof

jemalloc : $(MALLOC_STATICLIB)

update3rd :
	rm -rf 3rd/jemalloc && git submodule update --init

# skynet

CSERVICE = snlua logger gate harbor
LUA_CLIB = skynet \
  client \
  bson md5 sproto lpeg $(TLS_MODULE)

LUA_CLIB_SKYNET = \
  lua-skynet.c lua-seri.c \
  lua-socket.c \
  lua-mongo.c \
  lua-netpack.c \
  lua-memory.c \
  lua-multicast.c \
  lua-cluster.c \
  lua-crypt.c lsha1.c \
  lua-sharedata.c \
  lua-stm.c \
  lua-debugchannel.c \
  lua-datasheet.c \
  lua-sharetable.c \
  \

SKYNET_SRC = skynet_main.c skynet_handle.c skynet_module.c skynet_mq.c \
  skynet_server.c skynet_start.c skynet_timer.c skynet_error.c \
  skynet_harbor.c skynet_env.c skynet_monitor.c skynet_socket.c socket_server.c \
  malloc_hook.c skynet_daemon.c skynet_log.c

all : \
  $(SKYNET_BUILD_PATH)/skynet \
  $(foreach v, $(CSERVICE), $(CSERVICE_PATH)/$(v).so) \
  $(foreach v, $(LUA_CLIB), $(LUA_CLIB_PATH)/$(v).so)

define HAVE_BUILTIN
  CFLAGS += $(shell echo "#include<$1.h>\nint main(){return sizeof($2);}" | $(CC) -x c -o /dev/null - >/dev/null 2>&1 && echo "-D$3")
endef
$(eval $(call HAVE_BUILTIN,stdio,asprintf,HAVE_ASPRINTF))
$(eval $(call HAVE_BUILTIN,stdio,vasprintf,HAVE_VASPRINTF))
$(eval $(call HAVE_BUILTIN,string,strdup,HAVE_STRDUP))
$(eval $(call HAVE_BUILTIN,string,strndup,HAVE_STRNDUP))

SKYNET_SRCS = $(addprefix skynet-src/,$(SKYNET_SRC))
SKYNET_OBJS = $(SKYNET_SRCS:.c=.o)
SKYNET_DEPS = $(SKYNET_OBJS:.o=.d)
$(SKYNET_BUILD_PATH)/skynet: CFLAGS += -Iskynet-src -I$(JEMALLOC_INC)
$(SKYNET_BUILD_PATH)/skynet: $(SKYNET_OBJS) $(LUA_LIB) $(MALLOC_STATICLIB)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS) $(EXPORT) $(SKYNET_LIBS) $(SKYNET_DEFINES)
sinclude $(SKYNET_DEPS)
CLEAN_FILES += $(SKYNET_BUILD_PATH)/skynet $(SKYNET_OBJS) $(SKYNET_DEPS)

$(LUA_CLIB_PATH) :
	mkdir $(LUA_CLIB_PATH)

$(CSERVICE_PATH) :
	mkdir $(CSERVICE_PATH)

define CDYLIB_TEMP
  CDYLIB_$1_$2_OBJS:=$$(patsubst %.c,%.o,$3)
  CDYLIB_$1_$2_DEPS:=$$(CDYLIB_$1_$2_OBJS:.o=.d)
  $1/$2.so: CFLAGS += -fPIC $4
  $1/$2.so: $$(CDYLIB_$1_$2_OBJS) | $1
	$$(CC) $$(CFLAGS) $$(SHARED) $$^ -o $$@
  sinclude $$(CDYLIB_$1_$2_DEPS)
  CLEAN_FILES+=$1/$2.so $$(CDYLIB_$1_$2_OBJS) $$(CDYLIB_$1_$2_DEPS)
endef

define CSERVICE_TEMP
  $(call CDYLIB_TEMP,$$(CSERVICE_PATH),$1,service-src/service_$1.c,-Iskynet-src)
endef

$(foreach v, $(CSERVICE), $(eval $(call CSERVICE_TEMP,$(v))))

define CLUALIB_TEMP
  $(call CDYLIB_TEMP,$$(LUA_CLIB_PATH),$1,$2,$3)
endef

$(eval $(call CLUALIB_TEMP,skynet,$(addprefix lualib-src/,$(LUA_CLIB_SKYNET)),-Iskynet-src -Iservice-src))
$(eval $(call CLUALIB_TEMP,client,$(addprefix lualib-src/,lua-clientsocket.c lua-crypt.c lsha1.c),-lpthread))
$(eval $(call CLUALIB_TEMP,bson,lualib-src/lua-bson.c,-Iskynet-src))
$(eval $(call CLUALIB_TEMP,ltls,lualib-src/ltls.c,-Iskynet-src -I$(TLS_INC) -L$(TLS_LIB) -lssl))
$(eval $(call CLUALIB_TEMP,sproto,$(wildcard lualib-src/sproto/*.c)))
$(eval $(call CLUALIB_TEMP,md5,$(wildcard 3rd/lua-md5/*.c)))
$(eval $(call CLUALIB_TEMP,lpeg,$(wildcard 3rd/lpeg/*.c)))

clean :
	rm -f $(CLEAN_FILES) && \
  rm -rf $(SKYNET_BUILD_PATH)/*.dSYM $(CSERVICE_PATH)/*.dSYM $(LUA_CLIB_PATH)/*.dSYM

cleanall: clean
ifneq (,$(wildcard 3rd/jemalloc/Makefile))
	 $(MAKE) -C 3rd/jemalloc clean && rm 3rd/jemalloc/Makefile
endif
	$(MAKE) -C 3rd/lua clean
	rm -f $(LUA_STATICLIB)
