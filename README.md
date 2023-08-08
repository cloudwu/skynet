## ![skynet logo](https://github.com/cloudwu/skynet/wiki/image/skynet_metro.jpg)

Skynet is a multi-user Lua framework supporting the actor model, often used in games.

[It is heavily used in the Chinese game industry](https://github.com/cloudwu/skynet/wiki/Uses), but is also now spreading to other industries, and to English-centric developers. To visit related sites, visit the Chinese pages using something like Google or Deepl translate.

The community is friendly and almost contributors can speak English, so English speakers are welcome to ask questions in [Discussion](https://github.com/cloudwu/skynet/discussions), or sumbit issues in English.

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

Skynet now uses a modified version of lua 5.4.4 ( https://github.com/ejoy/lua/tree/skynet54 ) for multiple lua states.

Official Lua versions can also be used as long as the Makefile is edited.

## How To Use

* Read Wiki for documents https://github.com/cloudwu/skynet/wiki (Written in both English and Chinese)
* The FAQ in wiki https://github.com/cloudwu/skynet/wiki/FAQ (In Chinese, but you can visit them using something like Google or Deepl translate.)
