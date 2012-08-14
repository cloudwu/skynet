local skynet = require "skynet"
local c = require "socket.c"

local socket = {}
local fd
local object

function socket.connect(addr)
	local ip, port = string.match(addr,"([^:]+):(.+)")
	port = tonumber(port)
	fd = c.open(ip,port)
	skynet.send(".connection","ADD "..fd.." "..skynet.self())
	object = c.new()
end

function socket.push(msg,sz)
	if msg == nil then
		socket.close()
	else
		c.push(object, msg, sz)
	end
end

function socket.read(bytes)
	return c.read(object, bytes)
end

function socket.readline(sep)
	return c.readline(object, sep)
end

function socket.write(...)
	c.write(fd, ...)
end

function socket.yield()
	c.yield(object)
end

function socket.close()
	skynet.send(".connection","DEL "..fd)
	fd = nil
end

return socket

