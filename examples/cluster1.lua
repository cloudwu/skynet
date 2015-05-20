local skynet = require "skynet"
local cluster = require "cluster"
require "skynet.manager"	-- import skynet.name
local snax = require "snax"

skynet.start(function()
	local sdb = skynet.newservice("simpledb")
	skynet.name(".simpledb", sdb)
	print(skynet.call(".simpledb", "lua", "SET", "a", "foobar"))
	print(skynet.call(".simpledb", "lua", "SET", "b", "foobar2"))
	print(skynet.call(".simpledb", "lua", "GET", "a"))
	print(skynet.call(".simpledb", "lua", "GET", "b"))
	cluster.open "db"
	cluster.open "db2"
	-- unique snax service
	snax.uniqueservice "pingserver"
end)
