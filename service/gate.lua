local skynet = require "skynet"
local netpack = require "netpack"
local socketdriver = require "socketdriver"

local socket
local queue
local CMD = setmetatable({}, { __gc = function() netpack.clear(queue) end })

function CMD.open( conf )
	assert(not socket)
	local address = conf.address or "0.0.0.0"
	local port = assert(conf.port)
	local maxclient = conf.maxclient or 1024
	socket = socketdriver.listen(address, port)
	socketdriver.start(socket)
end

function CMD.close()
	assert(socket)
	socketdriver.close(socket)
	socket = nil
end

local MSG = {}

function MSG.data(fd, msg, sz)
	print("Data:", fd, netpack.tostring(msg, sz))
end

function MSG.more()
	for fd, msg, sz in netpack.pop, queue do
		print("More:", fd, netpack.tostring(msg, sz))
	end
end

function MSG.open(fd, msg)
	socketdriver.start(fd)
	print("Open:", fd, msg)
end

function MSG.close(fd)
	print("Close:", fd)
end

function MSG.error(fd, msg)
	print("Error:", fd, msg)
end

skynet.register_protocol {
	name = "socket",
	id = skynet.PTYPE_SOCKET,	-- PTYPE_SOCKET = 6
	unpack = function ( msg, sz )
		return netpack.filter( queue, msg, sz)
	end,
	dispatch = function (_, _, q, type, ...)
		queue = q
		if type then
			MSG[type](...)
		end
	end
}

skynet.start(function()
	skynet.dispatch("lua", function (_,_, cmd, ...)
		local f = assert(CMD[cmd])
		skynet.ret(skynet.pack(f(...)))
	end)
end)
