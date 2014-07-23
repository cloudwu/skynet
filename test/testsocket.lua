local skynet = require "skynet"
local socket = require "socket"

local mode , id = ...

local function echo(id)
	socket.start(id)

	while true do
		local str = socket.read(id)
		if str then
			socket.write(id, str)
		else
			socket.close(id)
			return
		end
	end
end

if mode == "agent" then
	id = tonumber(id)

	skynet.start(function()
		skynet.fork(function()
			echo(id)
			skynet.exit()
		end)
	end)
else
	local function accept(id)
		socket.start(id)
		socket.write(id, "Hello Skynet\n")
		skynet.newservice(SERVICE_NAME, "agent", id)
		-- notice: Some data on this connection(id) may lost before new service start.
		-- So, be careful when you want to use start / abandon / start .
		socket.abandon(id)
	end

	skynet.start(function()
		local id = socket.listen("127.0.0.1", 8001)
		print("Listen socket :", "127.0.0.1", 8001)

		socket.start(id , function(id, addr)
			print("connect from " .. addr .. " " .. id)
			-- you have choices :
			-- 1. skynet.newservice("testsocket", "agent", id)
			-- 2. skynet.fork(echo, id)
			-- 3. accept(id)
			accept(id)
		end)
	end)
end