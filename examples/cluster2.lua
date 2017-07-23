local skynet = require "skynet"
local cluster = require "skynet.cluster"

skynet.start(function()
	-- query name "sdb" of cluster db.
	local sdb = cluster.query("db", "sdb")
	print("db.sbd=",sdb)
	local proxy = cluster.proxy("db", sdb)
	local largekey = string.rep("X", 128*1024)
	local largevalue = string.rep("R", 100 * 1024)
	print(skynet.call(proxy, "lua", "SET", largekey, largevalue))
	local v = skynet.call(proxy, "lua", "GET", largekey)
	assert(largevalue == v)
	skynet.send(proxy, "lua", "PING", "proxy")

	print(cluster.call("db", sdb, "GET", "a"))
	print(cluster.call("db2", sdb, "GET", "b"))
	cluster.send("db2", sdb, "PING", "db2:longstring" .. largevalue)

	-- test snax service
	local pingserver = cluster.snax("db", "pingserver")
	print(pingserver.req.ping "hello")
end)
