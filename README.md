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