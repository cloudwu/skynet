local skynet = require "skynet"
local socket = require "skynet.socket"

local function server()
	local host
	host = socket.udp(function(str, from)
		print("server v4 recv", str, socket.udp_address(from))
		socket.sendto(host, from, "OK " .. str)
	end , "127.0.0.1", 8765)	-- bind an address
end

local function client()
	local c = socket.udp(function(str, from)
		print("client v4 recv", str, socket.udp_address(from))
	end)
	socket.udp_connect(c, "127.0.0.1", 8765)
	for i=1,20 do
		socket.write(c, "hello " .. i)	-- write to the address by udp_connect binding
	end
end

local function server_v6()
	local server
	server = socket.udp_listen("::1", 8766, function(str, from)
		print(string.format("server_v6 recv str:%s from:%s", str, socket.udp_address(from)))
		socket.sendto(server, from, "OK " .. str)
	end)	-- bind an address
	print("create server succeed. "..server)
	return server
end

local function client_v6()
	local c = socket.udp_dial("::1", 8766, function(str, from)
		print(string.format("client recv v6 response str:%s from:%s", str, socket.udp_address(from)))
	end)
	
	print("create client succeed. "..c)
	for i=1,20 do
		socket.write(c, "hello " .. i)	-- write to the address by udp_connect binding
	end
end

skynet.start(function()
	skynet.fork(server)
	skynet.fork(client)
	skynet.fork(server_v6)
	skynet.fork(client_v6)
end)
