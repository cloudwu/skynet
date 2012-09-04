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
./client 127.0.0.1 8888	# Launch a client, and try to input some words.
```

## Blog (in Chinese)

* http://blog.codingnow.com/2012/09/the_design_of_skynet.html
* http://blog.codingnow.com/2012/08/skynet.html
* http://blog.codingnow.com/2012/08/skynet_harbor_rpc.html
