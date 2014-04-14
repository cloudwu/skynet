local skynet = require "skynet"
local netpack = require "netpack"
local socketdriver = require "socketdriver"

local socket
local queue
local watchdog
local maxclient
local client_number = 0
local CMD = setmetatable({}, { __gc = function() netpack.clear(queue) end })
local forwarding_address = {}
local forwarding_client = {}
local forwarding_map = {}
local openning = {}

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

local function unforward_fd(fd)
	local address = forwarding_address[fd]
	if address then
		local client = forwarding_client[fd]
		local fm = forwarding_map[address]
		if type(fm) == "table" then
			fm[address] = nil
			if next(fm) == nil then
				forwarding_map[address] = nil
			end
		else
			forwarding_map[address] = nil
		end
		forwarding_address[fd] = nil
		forwarding_client[fd] = nil
		return address, client
	end
end


function CMD.forward(source, fd, client, address)
	local addr = address or source
	if not unforward_fd(fd) then
		socketdriver.start(fd)
	end

	forwarding_address[fd] = addr
	forwarding_client[fd] = client or 0
	local fm = forwarding_map[addr]
	if fm then
		if type(fm) == "table" then
			fm[addr] = true
		else
			fm[addr] = { [fm] = true, [addr] = true }
		end
	else
		forwarding_map[addr] = fd
	end
end

function CMD.kick(source, fd)
	if not fd then
		fd = forwarding_map[source]
		if type(fd) == "table" then
			for k in pairs(fd) do
				socketdriver.close(k)
			end
			return
		end
	end
	socketdriver.close(fd)
end

local MSG = {}

function MSG.data(fd, msg, sz)
	local address = forwarding_address[fd]
	local client = forwarding_client[fd]
	skynet.redirect(address, client, "client", 0, msg, sz)
end

function MSG.more()
	for fd, msg, sz in netpack.pop, queue do
		local address = forwarding_address[fd]
		local client = forwarding_client[fd]
		skynet.redirect(address, client, "client", 0, msg, sz)
	end
end

function MSG.open(fd, msg)
	if client_number >= maxclient then
		socketdriver.close(fd)
		return
	end
	openning[fd] = true
	client_number = client_number + 1
	skynet.send(watchdog, "lua", "socket", "open", fd, msg)
end

local function close_fd(fd)
	local address, client = unforward_fd(fd)
	if openning[fd] then
		openning[fd] = nil
		client_number = client_number - 1
	end
end

function MSG.close(fd)
	close_fd(fd)
	skynet.send(watchdog, "lua", "socket", "close", fd)
end

function MSG.error(fd, msg)
	close_fd(fd)
	skynet.send(watchdog, "lua", "socket", "error", fd)
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
