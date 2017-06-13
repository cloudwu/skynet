local skynet = require "skynet"
local mc = require "skynet.multicast"
local dc = require "skynet.datacenter"

local mode = ...

if mode == "sub" then

skynet.start(function()
	skynet.dispatch("lua", function (_,_, cmd, channel)
		assert(cmd == "init")
		local c = mc.new {
			channel = channel ,
			dispatch = function (channel, source, ...)
				print(string.format("%s <=== %s %s",skynet.address(skynet.self()),skynet.address(source), channel), ...)
			end
		}
		print(skynet.address(skynet.self()), "sub", c)
		c:subscribe()
		skynet.ret(skynet.pack())
	end)
end)

else

skynet.start(function()
	local channel = mc.new()
	print("New channel", channel)
	for i=1,10 do
		local sub = skynet.newservice(SERVICE_NAME, "sub")
		skynet.call(sub, "lua", "init", channel.channel)
	end

	dc.set("MCCHANNEL", channel.channel)	-- for multi node test

	print(skynet.address(skynet.self()), "===>", channel)
	channel:publish("Hello World")
end)

end