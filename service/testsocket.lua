local skynet = require "skynet"
local socket = require "socket"

local function accepter(id, addr)
	print("connect from " .. addr .. " " .. id)
	socket.accept(id)
	socket.write(id, "Hello Skynet\n")
	while true do
		local str = socket.readline(id,"\n")
		if str then
			socket.write(id, str .. "\n")
		else
			socket.close(id)
			print("closed " .. id)
			return
		end
	end
end

skynet.start(function()
	socket.listen("127.0.0.1", 8000, accepter)
end)
