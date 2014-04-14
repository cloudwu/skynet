package.cpath = "luaclib/?.so"

local socket = require "clientsocket"
local cjson = require "cjson"

local fd = socket.connect("127.0.0.1", 8888)

local last
local result = {}

local function dispatch()
	while true do
		local status
		status, last = socket.recv(fd, last, result)
		if status == nil then
			error "Server closed"
		end
		if not status then
			break
		end
		for _, v in ipairs(result) do
			print(v)
		end
	end
end

while true do
	dispatch()
	local cmd = socket.readline()
	if cmd then
		socket.send(fd, cmd)
	else
		socket.usleep(100)
	end
end