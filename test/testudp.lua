local skynet = require "skynet"
local socket = require "socket"

local function server()
	local host
	host = socket.udp(function(data, sz, from)
		print("server recv", skynet.tostring(data,sz), socket.udp_address(from))
		socket.sendto(host, from, "OK")
	end , "127.0.0.1", 8765)	-- bind an address
end

local function client()
	local c = socket.udp(function(data, sz, from)
		print("client recv", skynet.tostring(data,sz), socket.udp_address(from))
	end)
	socket.udp_connect(c, "127.0.0.1", 8765)
	socket.write(c, "hello")	-- write to the address by udp_connect binding
end

skynet.start(function()
	skynet.fork(server)
	skynet.fork(client)
end)
