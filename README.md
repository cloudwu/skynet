## Build

For Linux, install autoconf first for jemalloc:

```
git clone https://github.com/cloudwu/skynet.git
cd skynet
make 'PLATFORM'  # PLATFORM can be linux, macosx, freebsd now
```

Or you can:

```
export PLAT=linux
make
```

For FreeBSD , use gmake instead of make.

## Test

Run these in different consoles:

```
./skynet examples/config	# Launch first skynet node  (Gate server) and a skynet-master (see config for standalone option)
./3rd/lua/lua examples/client.lua 	# Launch a client, and try to input hello.
```

## About Lua

Skynet now uses a modified version of lua 5.3.2 ( http://www.lua.org/ftp/lua-5.3.2.tar.gz ) .

For details: http://lua-users.org/lists/lua-l/2014-03/msg00489.html

You can also use other official Lua versions, just edit the Makefile by yourself.

## How To Use (Sorry, Only in Chinese now)

* Read Wiki https://github.com/cloudwu/skynet/wiki
* The FAQ in wiki https://github.com/cloudwu/skynet/wiki/FAQ
