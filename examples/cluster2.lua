local skynet = require "skynet"
local cluster = require "cluster"

skynet.start(function()
	local proxy = cluster.proxy("db", ".simpledb")
	local largekey = string.rep("X", 128*1024)
	local largevalue = string.rep("R", 100 * 1024)
	print(skynet.call(proxy, "lua", "SET", largekey, largevalue))
	local v = skynet.call(proxy, "lua", "GET", largekey)
	assert(largevalue == v)

	print(cluster.call("db", ".simpledb", "GET", "a"))
	print(cluster.call("db2", ".simpledb", "GET", "b"))

	-- test snax service
	local pingserver = cluster.snax("db", "pingserver")
	print(pingserver.req.ping "hello")
end)
