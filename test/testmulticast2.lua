local skynet = require "skynet"
local dc = require "datacenter"
local mc = require "multicast"

skynet.start(function()
	print("remote start")
	skynet.monitor("simplemonitor", true)
	local console = skynet.newservice("console")
	local channel = dc.get "MCCHANNEL"
	for i=1,10 do
		local sub = skynet.newservice("testmulticast", "sub")
		skynet.call(sub, "lua", "init", channel)
	end
	local c = mc.new {
		channel = channel ,
		dispatch = function(...) print("======>", ...) end,
	}
	c:subscribe()
	c:publish("Remote message")
	c:unsubscribe()
	c:publish("Remote message2")
	c:delete()
	skynet.exit()
end)
