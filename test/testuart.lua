local skynet = require "skynet"
local uart = require "uart"

skynet.start(function()
	local cb = function(id,data)
		print("recv data:"..data)
	end
	uart.open(cb,"/dev/ttyS1")
end)