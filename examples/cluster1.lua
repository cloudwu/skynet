local skynet = require "skynet"
local cluster = require "cluster"

skynet.start(function()
	skynet.newservice("simpledb")
	print(skynet.call("SIMPLEDB", "lua", "SET", "a", "foobar"))
	print(skynet.call("SIMPLEDB", "lua", "GET", "a"))
	cluster.open "db"
end)
