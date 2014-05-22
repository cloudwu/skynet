local skynet = require "skynet"
local cluster = require "cluster"

skynet.start(function()
	skynet.newservice("simpledb")
	skynet.call("SIMPLEDB", "lua", "SET", "a", "foobar")
	cluster.open(2528)
end)
