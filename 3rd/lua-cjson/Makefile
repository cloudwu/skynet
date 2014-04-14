##### Available defines for CJSON_CFLAGS #####
##
## USE_INTERNAL_ISINF:      Workaround for Solaris platforms missing isinf().
## DISABLE_INVALID_NUMBERS: Permanently disable invalid JSON numbers:
##                          NaN, Infinity, hex.
##
## Optional built-in number conversion uses the following defines:
## USE_INTERNAL_FPCONV:     Use builtin strtod/dtoa for numeric conversions.
## IEEE_BIG_ENDIAN:         Required on big endian architectures.
## MULTIPLE_THREADS:        Must be set when Lua CJSON may be used in a
##                          multi-threaded application. Requries _pthreads_.

##### Build defaults #####
LUA_VERSION =       5.1
TARGET =            cjson.so
PREFIX =            /usr/local
#CFLAGS =            -g -Wall -pedantic -fno-inline
CFLAGS =            -O3 -Wall -pedantic -DNDEBUG
CJSON_CFLAGS =      -fpic
CJSON_LDFLAGS =     -shared
LUA_INCLUDE_DIR =   $(PREFIX)/include
LUA_CMODULE_DIR =   $(PREFIX)/lib/lua/$(LUA_VERSION)
LUA_MODULE_DIR =    $(PREFIX)/share/lua/$(LUA_VERSION)
LUA_BIN_DIR =       $(PREFIX)/bin

##### Platform overrides #####
##
## Tweak one of the platform sections below to suit your situation.
##
## See http://lua-users.org/wiki/BuildingModules for further platform
## specific details.

## Linux

## FreeBSD
#LUA_INCLUDE_DIR =   $(PREFIX)/include/lua51

## MacOSX (Macports)
#PREFIX =            /opt/local
#CJSON_LDFLAGS =     -bundle -undefined dynamic_lookup

## Solaris
#CC           =      gcc
#CJSON_CFLAGS =      -fpic -DUSE_INTERNAL_ISINF

## Windows (MinGW)
#TARGET =            cjson.dll
#PREFIX =            /home/user/opt
#CJSON_CFLAGS =      -DDISABLE_INVALID_NUMBERS
#CJSON_LDFLAGS =     -shared -L$(PREFIX)/lib -llua51
#LUA_BIN_SUFFIX =    .lua

##### Number conversion configuration #####

## Use Libc support for number conversion (default)
FPCONV_OBJS =       fpconv.o

## Use built in number conversion
#FPCONV_OBJS =       g_fmt.o dtoa.o
#CJSON_CFLAGS +=     -DUSE_INTERNAL_FPCONV

## Compile built in number conversion for big endian architectures
#CJSON_CFLAGS +=     -DIEEE_BIG_ENDIAN

## Compile built in number conversion to support multi-threaded
## applications (recommended)
#CJSON_CFLAGS +=     -pthread -DMULTIPLE_THREADS
#CJSON_LDFLAGS +=    -pthread

##### End customisable sections #####

TEST_FILES =        README bench.lua genutf8.pl test.lua octets-escaped.dat \
                    example1.json example2.json example3.json example4.json \
                    example5.json numbers.json rfc-example1.json \
                    rfc-example2.json types.json
DATAPERM =          644
EXECPERM =          755

ASCIIDOC =          asciidoc

BUILD_CFLAGS =      -I$(LUA_INCLUDE_DIR) $(CJSON_CFLAGS)
OBJS =              lua_cjson.o strbuf.o $(FPCONV_OBJS)

.PHONY: all clean install install-extra doc

.SUFFIXES: .html .txt

.c.o:
	$(CC) -c $(CFLAGS) $(CPPFLAGS) $(BUILD_CFLAGS) -o $@ $<

.txt.html:
	$(ASCIIDOC) -n -a toc $<

all: $(TARGET)

doc: manual.html performance.html

$(TARGET): $(OBJS)
	$(CC) $(LDFLAGS) $(CJSON_LDFLAGS) -o $@ $(OBJS)

install: $(TARGET)
	mkdir -p $(DESTDIR)/$(LUA_CMODULE_DIR)
	cp $(TARGET) $(DESTDIR)/$(LUA_CMODULE_DIR)
	chmod $(EXECPERM) $(DESTDIR)/$(LUA_CMODULE_DIR)/$(TARGET)

install-extra:
	mkdir -p $(DESTDIR)/$(LUA_MODULE_DIR)/cjson/tests \
		$(DESTDIR)/$(LUA_BIN_DIR)
	cp lua/cjson/util.lua $(DESTDIR)/$(LUA_MODULE_DIR)/cjson
	chmod $(DATAPERM) $(DESTDIR)/$(LUA_MODULE_DIR)/cjson/util.lua
	cp lua/lua2json.lua $(DESTDIR)/$(LUA_BIN_DIR)/lua2json$(LUA_BIN_SUFFIX)
	chmod $(EXECPERM) $(DESTDIR)/$(LUA_BIN_DIR)/lua2json$(LUA_BIN_SUFFIX)
	cp lua/json2lua.lua $(DESTDIR)/$(LUA_BIN_DIR)/json2lua$(LUA_BIN_SUFFIX)
	chmod $(EXECPERM) $(DESTDIR)/$(LUA_BIN_DIR)/json2lua$(LUA_BIN_SUFFIX)
	cd tests; cp $(TEST_FILES) $(DESTDIR)/$(LUA_MODULE_DIR)/cjson/tests
	cd tests; chmod $(DATAPERM) $(TEST_FILES); chmod $(EXECPERM) *.lua *.pl

clean:
	rm -f *.o $(TARGET)
