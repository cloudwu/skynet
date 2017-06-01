local skynet = require "skynet"
local kvdb = require "kvdb"

skynet.start(function()
	kvdb.set("A", 1)
	print(kvdb.get "A")
end)
