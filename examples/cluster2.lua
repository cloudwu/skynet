local skynet = require "skynet"
local cluster = require "skynet.cluster"

skynet.start(function()
	local proxy = cluster.proxy "db@sdb"	-- cluster.proxy("db", "@sdb")
	local largekey = string.rep("X", 128*1024)
	local largevalue = string.rep("R", 100 * 1024)
	skynet.call(proxy, "lua", "SET", largekey, largevalue)
	local v = skynet.call(proxy, "lua", "GET", largekey)
	assert(largevalue == v)
	skynet.send(proxy, "lua", "PING", "proxy")

	print(cluster.call("db", "@sdb", "GET", "a"))
	print(cluster.call("db2", "@sdb", "GET", "b"))
	cluster.send("db2", "@sdb", "PING", "db2:longstring" .. largevalue)

	-- test snax service
	skynet.timeout(300,function()
		cluster.reload {
			db = false,	-- db is down
			db3 = "127.0.0.1:2529"
		}
		print(pcall(cluster.call, "db", "@sdb", "GET", "a"))	-- db is down
	end)
	local pingserver = cluster.snax("db3", "pingserver")
	print(pingserver.req.ping "hello")
end)
