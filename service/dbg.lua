local skynet = require "skynet"

local cmd = { ... }

skynet.start(function()
	local list = skynet.call(".launcher","lua", unpack(cmd))
	for k,v in pairs(list) do
		print(k,v)
	end
	skynet.exit()
end)