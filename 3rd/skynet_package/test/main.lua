local skynet = require "skynet"
local socket = require "socket"
local proxy = require "socket_proxy"

local function read(fd)
	return skynet.tostring(proxy.read(fd))
end

skynet.start(function()
	skynet.newservice("debug_console",8000)
	local id = assert(socket.listen("127.0.0.1", 8888))
	socket.start(id, function (fd, addr)
		skynet.error(string.format("%s connected as %d" , addr, fd))
		proxy.subscribe(fd)
		while true do
			local ok, s = pcall(read, fd)
			if not ok then
				skynet.error("CLOSE")
				break
			end
			if s == "quit" then
				proxy.close(fd)
				break
			end
			skynet.error(s)
		end
	end)
end)
