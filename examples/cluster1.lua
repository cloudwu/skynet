local skynet = require "skynet"
local cluster = require "cluster"

skynet.start(function()
	local sdb = skynet.newservice("simpledb")
	skynet.name(".simpledb", sdb)
	print(skynet.call(".simpledb", "lua", "SET", "a", "foobar"))
	print(skynet.call(".simpledb", "lua", "GET", "a"))
	cluster.open "db"
end)
