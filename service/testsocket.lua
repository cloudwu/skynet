local skynet = require "skynet"
local socket = require "socket"

local function accepter(id)
	socket.start(id)
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
	local id = socket.listen("127.0.0.1", 8000)

	socket.start(id , function(id, addr)
		print("connect from " .. addr .. " " .. id)
		-- you can also call skynet.newservice for this socket id
		skynet.fork(accepter, id)
	end)
end)
