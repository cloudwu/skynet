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

Skynet now use Lua 5.3.0 final, http://www.lua.org/ftp/lua-5.3.0.tar.gz

You can also use the other official Lua version , edit the makefile by yourself .

## How To (in Chinese)

* Read Wiki https://github.com/cloudwu/skynet/wiki
* The FAQ in wiki https://github.com/cloudwu/skynet/wiki/FAQ
