local skynet = require "skynet"
local socket = require "skynet.socket"

local function server()
	local host
	local on_packet = function(str, from)
		print("server recv", str, socket.udp_address(from))
		socket.sendto(host, from, "OK " .. str)
	end

	-- bind an address
	host = socket.udp(on_packet, "127.0.0.1", 8765)
end

local function client()
	local on_packet = function(str, from)
		print("client recv", str, socket.udp_address(from))
	end

	local cli = socket.udp(on_packet)
	socket.udp_connect(cli, "127.0.0.1", 8765)

	for i=1, 20 do
		-- write to the address by udp_connect binding
		socket.write(cli, "hello " .. i)
		skynet.sleep(50)
	end
end

skynet.start(function()
	skynet.fork(server)
	skynet.fork(client)
end)
