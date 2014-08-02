## Build

For linux, install autoconf first for jemalloc

```
git clone https://github.com/cloudwu/skynet.git
cd skynet
make 'PLATFORM'  # PLATFORM can be linux, macosx, freebsd now
```

Or you can :

```
export PLAT=linux
make
```

For freeBSD , use gmake instead of make .

## Test

Run these in different console

```
./skynet examples/config	# Launch first skynet node  (Gate server) and a skynet-master (see config for standalone option)
lua examples/client.lua 	# Launch a client, and try to input some words.
```

## About Lua

Skynet put a modified version of lua 5.2.3 in 3rd/lua , it can share proto type between lua state (http://lua-users.org/lists/lua-l/2014-03/msg00489.html) .

Each lua file only load once and cache it in memory during skynet start . so if you want to reflush the cache , call skynet.cache.clear() .

You can also use the offical lua version , edit the makefile by yourself .

## How To (in Chinese)

* Read Wiki https://github.com/cloudwu/skynet/wiki
