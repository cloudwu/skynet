local socket = require "socket"

local readline = socket.readline
local readbytes = socket.read
local writebytes = socket.write

local sockethelper = {}
local socket_error = {}

sockethelper.socket_error = socket_error

function sockethelper.readfunc(fd)
	local helper = {}

	function helper.readline()
		local ret = readline(fd, "\r\n")
		if ret then
			return ret
		else
			error(socket_error)
		end
	end

	function helper.readbytes(sz)
		local ret = readbytes(fd, sz)
		if ret then
			return ret
		else
			error(socket_error)
		end
	end

	return helper
end

function sockethelper.writefunc(fd)
	return function(content)
		local ok = writebytes(fd, content)
		if not ok then
			error(socket_error)
		end
	end
end

return sockethelper