local skynet = require "skynet"
local socket = require "socket"

local mode , id = ...

if mode == "agent" then
	id = tonumber(id)

	skynet.start(function()
		socket.start(id)

		while true do
			local str = socket.readline(id,"\n")
			if str then
				socket.write(id, str .. "\n")
			else
				socket.close(id)
				print("closed " .. id)
				break
			end
		end

		skynet.exit()
	end)
else
	local function accepter(id)
		socket.start(id)
		socket.write(id, "Hello Skynet\n")
		skynet.newservice("testsocket", "agent", id)
		-- notice: Some data on this connection(id) may lost before new service start.
		-- So, be careful when you want to use start / abandon / start .
		socket.abandon(id)
	end

	skynet.start(function()
		local id = socket.listen("127.0.0.1", 8000)

		socket.start(id , function(id, addr)
			print("connect from " .. addr .. " " .. id)
			-- you can also call skynet.newservice for this socket id
			skynet.fork(accepter, id)
		end)
	end)
end