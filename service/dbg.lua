local skynet = require "skynet"
local print_r = require "print_r"

local cmd = { ... }

skynet.start(function()
	local list = skynet.call(".launcher","lua", unpack(cmd))
	if list then
		print_r(list)
	end
	skynet.exit()
end)