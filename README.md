## Build

Install zeromq 2.2 and lua 5.2 first.

```
make
```

## Test

Run these in different console

```
./skynet-master		# Launch master server first
./skynet config		# Launch first skynet node  (Gate server)
./skynet config_log	# Launch second skynet node (Global logger server)
./clinet 127.0.0.1 8888	# Launch a client, and try to input some words.
```

## Test connection server

```
./skynet-master		# Launch master server first
./skynet 		# Launch first skynet node 
./nc -l 8000		# Listen on port 8000 
```

And then , type 'snlua testconn.lua' in skynet console to launch connection server and a test program.

You can type something in nc console.

## Blog (in Chinese)

* http://blog.codingnow.com/2012/08/skynet.html
* http://blog.codingnow.com/2012/08/skynet_harbor_rpc.html
