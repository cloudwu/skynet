local skynet = require "skynet"
local mc = require "multicast"

local mode = ...

if mode == "sub" then

skynet.start(function()
	skynet.dispatch("lua", function (_,_, cmd, channel)
		assert(cmd == "init")
		mc.subscribe(channel, {
			dispatch = function (channel, source, ...)
				print(string.format("%s <=== %s (%d)",skynet.address(skynet.self()),skynet.address(source), channel), ...)
			end
		})
	end)
end)

else

skynet.start(function()
	local channel = mc.newchannel()
	print("New channel", channel)
	for i=1,10 do
		local sub = skynet.newservice("testmulticast", "sub")
		skynet.send(sub, "lua", "init", channel)
	end

	print(skynet.address(skynet.self()), "===>", channel)
	mc.publish(channel, "Hello World")
end)

end