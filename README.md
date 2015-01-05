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
./3rd/lua/lua examples/client.lua 	# Launch a client, and try to input hello.
```

## About Lua

Skynet now use lua 5.3.0-rc3 , http://www.lua.org/work/lua-5.3.0-rc3.tar.gz

You can also use the other offical lua version , edit the makefile by yourself .

## How To (in Chinese)

* Read Wiki https://github.com/cloudwu/skynet/wiki
