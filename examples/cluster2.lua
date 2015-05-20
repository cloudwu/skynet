local skynet = require "skynet"
local cluster = require "cluster"

skynet.start(function()
	local proxy = cluster.proxy("db", ".simpledb")
	print(skynet.call(proxy, "lua", "GET", "a"))
	print(cluster.call("db", ".simpledb", "GET", "a"))
	print(cluster.call("db2", ".simpledb", "GET", "b"))

	-- test snax service
	local pingserver = cluster.snax("db", "pingserver")
	print(pingserver.req.ping "hello")
end)
