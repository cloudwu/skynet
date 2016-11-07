local skynet = require "skynet"
local uart = require "uart"

local id

skynet.start(function()
	local cb = function(id,data)
		print("recv data:" .. data)
		if string.sub(data,1,-2) == "quit" then
			uart.close(fd)
			skynet.exit()
		end
		uart.send(id,"ok "..data)
	end
	id = uart.open (cb, "/dev/ttyS1")
	uart.set(id, 115200, 0, 8, 1, 'N')
end)