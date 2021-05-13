这个版本和官方的版本完全一致，并且会一直合并最新的修改；由于skynet极其精简的内核，所以实现这个调试器并没有修改框架的代码，只是增加了几个额外的模块：

cjson 用于进行json格式的数据交换。

-------------------------------------------------------------------------------------------------------------------------
## ![skynet logo](https://github.com/cloudwu/skynet/wiki/image/skynet_metro.jpg)

Skynet is a lightweight online game framework which can be used in many other fields.

## Build

For Linux, install autoconf first for jemalloc:

```
git clone https://github.com/cloudwu/skynet.git
cd skynet
make 'PLATFORM'  # PLATFORM can be linux, macosx, freebsd now
```

Or:

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

## About Lua version

Skynet now uses a modified version of lua 5.3.5 ( https://github.com/ejoy/lua/tree/skynet ) for multiple lua states.

Official Lua versions can also be used as long as the Makefile is edited.

## How To Use (Sorry, currently only available in Chinese)

* Read Wiki for documents https://github.com/cloudwu/skynet/wiki
* The FAQ in wiki https://github.com/cloudwu/skynet/wiki/FAQ
