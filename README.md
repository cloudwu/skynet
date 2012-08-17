## Build

Install lua 5.2 first.

```
make
```

## Test

Run these in different console

```
./skynet config		# Launch first skynet node  (Gate server) and a skynet-master (see config for standalone option)
./skynet config_log	# Launch second skynet node (Global logger server)
./clinet 127.0.0.1 8888	# Launch a client, and try to input some words.
```

## Test connection server

```
./skynet 		# Launch skynet
./nc -l 8000		# Listen on port 8000 
```

And then , type 'snlua testconn' in skynet console to launch connection server and a test program.

You can type something in nc console.

## Blog (in Chinese)

* http://blog.codingnow.com/2012/08/skynet.html
* http://blog.codingnow.com/2012/08/skynet_harbor_rpc.html
