local socket = require "socket"

local readbytes = socket.read
local writebytes = socket.write

local sockethelper = {}
local socket_error = setmetatable({} , { __tostring = function() return "[Socket Error]" end })

sockethelper.socket_error = socket_error

function sockethelper.readfunc(fd)
	return function (sz)
		local ret = readbytes(fd, sz)
		if ret then
			return ret
		else
			error(socket_error)
		end
	end
end

function sockethelper.writefunc(fd)
	return function(content)
		local ok = writebytes(fd, content)
		if not ok then
			error(socket_error)
		end
	end
end

function sockethelper.connect(host, port)
	local fd = socket.open(host, port)
	if fd then
		return fd
	end
	error(socket_error)
end

function sockethelper.close(fd)
	socket.close(fd)
end

return sockethelper
