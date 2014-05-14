local skynet = require "skynet"
local netpack = require "netpack"
local socketdriver = require "socketdriver"

local socket
local queue
local watchdog
local maxclient
local client_number = 0
local CMD = setmetatable({}, { __gc = function() netpack.clear(queue) end })

local connection = {}	-- fd -> connection : { fd , client, agent , ip, mode }
local forwarding = {}	-- agent -> connection

function CMD.open( source , conf )
	assert(not socket)
	local address = conf.address or "0.0.0.0"
	local port = assert(conf.port)
	maxclient = conf.maxclient or 1024
	watchdog = conf.watchdog or source
	socket = socketdriver.listen(address, port)
	socketdriver.start(socket)
end

function CMD.close()
	assert(socket)
	socketdriver.close(socket)
	socket = nil
end

local function unforward(c)
	if c.agent then
		forwarding[c.agent] = nil
		c.agent = nil
		c.client = nil
	end
end

local function start(c)
	if not c.mode then
		c.mode = "open"
		socketdriver.start(c.fd)
	end
end

function CMD.forward(source, fd, client, address)
	local c = assert(connection[fd])
	unforward(c)
	start(c)

	c.client = client or 0
	c.agent = address or source

	forwarding[c.agent] = c
end

function CMD.accept(source, fd)
	local c = assert(connection[fd])
	unforward(c)
	start(c)
end

function CMD.kick(source, fd)
	local c
	if fd then
		c = connection[fd]
	else
		c = forwarding[source]
	end

	assert(c)

	if c.mode ~= "close" then
		c.mode = "close"
		socketdriver.close(c.fd)
	end
end

local MSG = {}

function MSG.data(fd, msg, sz)
	-- recv a package, forward it
	local c = connection[fd]
	local agent = c.agent
	if agent then
		skynet.redirect(agent, c.client, "client", 0, msg, sz)
	else
		skynet.send(watchdog, "lua", "socket", "data", fd, netpack.tostring(msg, sz))
	end
end

function MSG.more()
	for fd, msg, sz in netpack.pop, queue do
		MSG.data(fd, msg, sz)
	end
end

function MSG.open(fd, msg)
	if client_number >= maxclient then
		socketdriver.close(fd)
		return
	end
	local c = {
		fd = fd,
		ip = msg,
	}
	connection[fd] = c
	client_number = client_number + 1
	skynet.send(watchdog, "lua", "socket", "open", fd, msg)
end

local function close_fd(fd, message)
	local c = connection[fd]
	if c then
		unforward(c)
		connection[fd] = nil
		client_number = client_number - 1
	end
end

function MSG.close(fd)
	close_fd(fd)
	skynet.send(watchdog, "lua", "socket", "close", fd)
end

function MSG.error(fd, msg)
	close_fd(fd)
	skynet.send(watchdog, "lua", "socket", "error", fd, msg)
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

skynet.register_protocol {
	name = "client",
	id = skynet.PTYPE_CLIENT,
}

skynet.start(function()
	skynet.dispatch("lua", function (_, address, cmd, ...)
		local f = assert(CMD[cmd])
		skynet.ret(skynet.pack(f(address, ...)))
	end)
end)
