# Cross-compilation toolchain
CC = x86_64-w64-mingw32-gcc
AR = x86_64-w64-mingw32-ar
RANLIB = x86_64-w64-mingw32-ranlib

LUA_CLIB_PATH ?= luaclib
CSERVICE_PATH ?= cservice
SKYNET_BUILD_PATH ?= .
COMPAT_MINGW_DIR = 3rd/compat-mingw

LUA_DIR = 3rd/lua
LUA_CFLAGS = -g -O2 -Wall -I. -std=gnu99 -I../../skynet-src
LUA_STATICLIB := 3rd/lua/liblua.a
LUA_DLL ?= 3rd/lua/lua54.dll
SKYNET_DLL ?= $(SKYNET_BUILD_PATH)/skynet.dll
LUA_INC ?= $(LUA_DIR)

# Lua linking options
LUA_LIBS = -L$(LUA_DIR) -llua54
LUA_STATIC_LIBS = $(LUA_STATICLIB)

# Skynet linking and include options
SKYNET_LINK_LIBS = -L$(SKYNET_BUILD_PATH) -lskynet
SKYNET_INCLUDES = -Iskynet-src -I$(COMPAT_MINGW_DIR) -I$(LUA_INC)
SHARED_BUILD = $(CC) $(CFLAGS) $(SHARED)

# Compiler flags
CFLAGS = -g -O2 -Wall -std=gnu99 -I$(LUA_INC) $(MYCFLAGS)
CFLAGS += -I3rd/lua -Iskynet-src -I$(COMPAT_MINGW_DIR)
CFLAGS += -include $(COMPAT_MINGW_DIR)/compat.h
CFLAGS += -Wno-int-conversion -Wno-incompatible-pointer-types -Wno-pointer-sign -Wno-unused-function

LDFLAGS = -Wl,--export-all-symbols,--out-implib,libskynet.a
SHARED = -fPIC --shared -Wl,--export-all-symbols,--enable-auto-import,--allow-shlib-undefined,--unresolved-symbols=ignore-all

SKYNET_LIBS = -static-libgcc -Wl,-Bstatic -lpthread -Wl,-Bdynamic -lm -lws2_32 -lgdi32

SKYNET_DEFINES = -DNOUSE_JEMALLOC

COMPAT_LIB = $(COMPAT_MINGW_DIR)/libcompat.a

CSERVICE = snlua logger gate harbor

LUA_CLIB = skynet client bson md5 sproto lpeg

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
  mem_info.c malloc_hook.c skynet_daemon.c skynet_log.c

$(LUA_STATICLIB): 
	@echo "Building Lua static library..."
	cd $(LUA_DIR) && $(CC) $(LUA_CFLAGS) -DMAKE_LIB -c onelua.c -o onelua.o
	cd $(LUA_DIR) && $(AR) rcs liblua.a onelua.o

$(LUA_DLL) : $(LUA_STATICLIB)
	@echo "Building Lua DLL..."
	cd $(LUA_DIR) && $(CC) -shared -o lua54.dll onelua.o -Wl,--export-all-symbols,--out-implib,liblua54.a $(SKYNET_LIBS)
	@echo "Lua DLL and import library created successfully"

$(LUA_DIR)/lua.exe : $(LUA_DLL) $(LUA_STATICLIB)
	@echo "Building Lua interpreter..."
	cd $(LUA_DIR) && $(CC) $(LUA_CFLAGS) -c lua.c -o lua.o
	cd $(LUA_DIR) && $(CC) $(LUA_CFLAGS) -o lua.exe lua.o -L. -llua54 $(SKYNET_LIBS)
	cp $(LUA_DLL) $(SKYNET_BUILD_PATH)/

$(LUA_DIR)/luac.exe : $(LUA_DLL) $(LUA_STATICLIB)
	@echo "Building Lua compiler..."
	cd $(LUA_DIR) && $(CC) $(LUA_CFLAGS) -DMAKE_LUAC -c onelua.c -o luac.o
	cd $(LUA_DIR) && $(CC) $(LUA_CFLAGS) -o luac.exe luac.o liblua.a $(SKYNET_LIBS)

$(COMPAT_LIB): $(COMPAT_MINGW_DIR)/compat.c
	@echo "Building compatibility library..."
	$(CC) $(CFLAGS) -c $(COMPAT_MINGW_DIR)/compat.c -o $(COMPAT_MINGW_DIR)/compat.o
	cd $(COMPAT_MINGW_DIR) && $(AR) rcs libcompat.a compat.o
	cd $(COMPAT_MINGW_DIR) && $(RANLIB) libcompat.a

# Build order: first build core libraries, then dependent modules
all : core_libs modules tools

.PHONY: core_libs modules tools

core_libs: $(LUA_STATICLIB) $(LUA_DLL) $(SKYNET_DLL)

modules: core_libs $(foreach v, $(CSERVICE), $(CSERVICE_PATH)/$(v).so) $(foreach v, $(LUA_CLIB), $(LUA_CLIB_PATH)/$(v).so)

tools: core_libs $(LUA_DIR)/lua.exe $(LUA_DIR)/luac.exe $(SKYNET_BUILD_PATH)/skynet.exe


SKYNET_LIB_SRC = $(filter-out skynet_main.c, $(SKYNET_SRC))


$(SKYNET_DLL) : $(foreach v, $(SKYNET_LIB_SRC), skynet-src/$(v)) $(COMPAT_LIB) $(LUA_DLL)
	@echo "Building skynet.dll..."
	$(CC) $(CFLAGS) -shared -o $@ $(foreach v, $(SKYNET_LIB_SRC), skynet-src/$(v)) $(COMPAT_LIB) $(LUA_LIBS) $(SKYNET_INCLUDES) $(SKYNET_LIBS) $(SKYNET_DEFINES) $(LDFLAGS)


$(SKYNET_BUILD_PATH)/skynet.exe : skynet-src/skynet_main.c $(SKYNET_DLL) $(COMPAT_LIB) $(LUA_DLL)
	@echo "Building skynet.exe..."
	$(CC) $(CFLAGS) -o $@ skynet-src/skynet_main.c $(COMPAT_LIB) $(SKYNET_LINK_LIBS) $(LUA_LIBS) $(SKYNET_INCLUDES) $(SKYNET_LIBS) $(SKYNET_DEFINES)
	cp $(LUA_DLL) $(SKYNET_BUILD_PATH)/

$(LUA_CLIB_PATH) :
	mkdir $(LUA_CLIB_PATH)

$(CSERVICE_PATH) :
	mkdir $(CSERVICE_PATH)

define CSERVICE_TEMP
  $$(CSERVICE_PATH)/$(1).so : service-src/service_$(1).c $$(LUA_DLL) $$(SKYNET_DLL) | $$(CSERVICE_PATH)
	$$(CC) $$(CFLAGS) $$(SHARED) $$< -o $$@ $$(SKYNET_INCLUDES) $$(LUA_LIBS) $$(SKYNET_LINK_LIBS)
endef

$(foreach v, $(CSERVICE), $(eval $(call CSERVICE_TEMP,$(v))))


$(LUA_CLIB_PATH)/skynet.so : $(addprefix lualib-src/,$(LUA_CLIB_SKYNET)) $(SKYNET_DLL) $(LUA_DLL) | $(LUA_CLIB_PATH)
	$(SHARED_BUILD) $(addprefix lualib-src/,$(LUA_CLIB_SKYNET)) -o $@ $(SKYNET_INCLUDES) -Iservice-src -Ilualib-src $(SKYNET_LINK_LIBS) $(LUA_LIBS) $(SKYNET_LIBS)

$(LUA_CLIB_PATH)/bson.so : lualib-src/lua-bson.c $(LUA_DLL) | $(LUA_CLIB_PATH)
	$(SHARED_BUILD) $^ -o $@ $(SKYNET_INCLUDES) $(LUA_LIBS) $(SKYNET_LIBS)

$(LUA_CLIB_PATH)/md5.so : 3rd/lua-md5/md5.c 3rd/lua-md5/md5lib.c 3rd/lua-md5/compat-5.2.c $(LUA_DLL) | $(LUA_CLIB_PATH)
	$(SHARED_BUILD) -I3rd/lua-md5 -Wno-attributes $^ -o $@ $(LUA_LIBS) $(SKYNET_LIBS)

$(LUA_CLIB_PATH)/client.so : lualib-src/lua-clientsocket.c lualib-src/lua-crypt.c lualib-src/lsha1.c $(COMPAT_LIB) $(LUA_DLL) | $(LUA_CLIB_PATH)
	$(SHARED_BUILD) $^ -o $@ $(LUA_LIBS) $(SKYNET_LIBS)

$(LUA_CLIB_PATH)/sproto.so : lualib-src/sproto/sproto.c lualib-src/sproto/lsproto.c $(LUA_DLL) | $(LUA_CLIB_PATH)
	$(SHARED_BUILD) -Ilualib-src/sproto $^ -o $@ $(LUA_LIBS) $(SKYNET_LIBS)

$(LUA_CLIB_PATH)/lpeg.so : 3rd/lpeg/lpcap.c 3rd/lpeg/lpcode.c 3rd/lpeg/lpprint.c 3rd/lpeg/lptree.c 3rd/lpeg/lpvm.c 3rd/lpeg/lpcset.c $(LUA_DLL) | $(LUA_CLIB_PATH)
	$(SHARED_BUILD) -I3rd/lpeg $^ -o $@ $(LUA_LIBS) $(SKYNET_LIBS)

.PHONY: clean cleanall core_libs modules tools

clean :
	rm -f $(SKYNET_BUILD_PATH)/*.exe $(SKYNET_BUILD_PATH)/*.dll $(SKYNET_BUILD_PATH)/*.a
	rm -f $(COMPAT_LIB) $(COMPAT_MINGW_DIR)/*.o
	rm -f $(CSERVICE_PATH)/*.so $(LUA_CLIB_PATH)/*.so
	rm -f $(LUA_DIR)/*.a

cleanall: clean
	rm -f $(LUA_STATICLIB)
	rm -f $(LUA_DIR)/*.exe $(LUA_DIR)/*.dll $(LUA_DIR)/*.a $(LUA_DIR)/*.o
