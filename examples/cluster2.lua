local skynet = require "skynet"
local cluster = require "cluster"

skynet.start(function()
	print(cluster.call("db", "SIMPLEDB", "GET", "a"))
end)
