local skynet = require "skynet"
local mqueue = require "mqueue"

skynet.start(function()
	local id = 0
	local pingserver = skynet.newservice "pingserver"
	mqueue.register(function(str)
		id = id + 1
		str = string.format("id = %d , %s",id, str)
		return skynet.call(pingserver, "lua", "PING", str)
	end)
end)
