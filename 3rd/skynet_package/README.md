## What's this ?

It's a example to show how to split the packages from a tcp data stream in skynet.

`service/socket_proxyd.lua` is a manager service of sockets, and `lualib/socket_proxy.lua` is the library. Read the test/main.lua for usecase.

If you want to manage a tcp data stream, you can call `proxy.subscribe(fd)` first. It register the fd into the manager (the unique service socket_proxyd) , and launch a C service for it to split the tcp data stream.

`proxy.read(fd)` would request a package (WORD in big endian + DATA) from the C service.

`proxy.write(fd)` would forward the package to the tcp socket.

These C services haven't list in debug console, but you can call `info` by `socket_proxyd` in debug console to show these services.

## Build

Modify the Makefile, change SKYNET_PATH to your path of skynet. The default path is $(HOME)/skynet .

If you are not using linux, make it by yourself.

## Test

Run skynet first :

```
$(HOME)/skynet test/config
```

And then use the simple client in ./test :

```
lua test/client.lua
```

The test/main.lua is a simple echo server, so you can try to type something now.
