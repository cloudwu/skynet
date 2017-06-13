local skynet = require "skynet"
local socket = require "skynet.socket"

--[[
	master manage data :
		1. all the slaves address : id -> ipaddr:port
		2. all the global names : name -> address

	master hold connections from slaves .

	protocol slave->master :
		package size 1 byte
		type 1 byte :
			'H' : HANDSHAKE, report slave id, and address.
			'R' : REGISTER name address
			'Q' : QUERY name


	protocol master->slave:
		package size 1 byte
		type 1 byte :
			'W' : WAIT n
			'C' : CONNECT slave_id slave_address
			'N' : NAME globalname address
			'D' : DISCONNECT slave_id
]]

local slave_node = {}
local global_name = {}

local function read_package(fd)
	local sz = socket.read(fd, 1)
	assert(sz, "closed")
	sz = string.byte(sz)
	local content = assert(socket.read(fd, sz), "closed")
	return skynet.unpack(content)
end

local function pack_package(...)
	local message = skynet.packstring(...)
	local size = #message
	assert(size <= 255 , "too long")
	return string.char(size) .. message
end

local function report_slave(fd, slave_id, slave_addr)
	local message = pack_package("C", slave_id, slave_addr)
	local n = 0
	for k,v in pairs(slave_node) do
		if v.fd ~= 0 then
			socket.write(v.fd, message)
			n = n + 1
		end
	end
	socket.write(fd, pack_package("W", n))
end

local function handshake(fd)
	local t, slave_id, slave_addr = read_package(fd)
	assert(t=='H', "Invalid handshake type " .. t)
	assert(slave_id ~= 0 , "Invalid slave id 0")
	if slave_node[slave_id] then
		error(string.format("Slave %d already register on %s", slave_id, slave_node[slave_id].addr))
	end
	report_slave(fd, slave_id, slave_addr)
	slave_node[slave_id] = {
		fd = fd,
		id = slave_id,
		addr = slave_addr,
	}
	return slave_id , slave_addr
end

local function dispatch_slave(fd)
	local t, name, address = read_package(fd)
	if t == 'R' then
		-- register name
		assert(type(address)=="number", "Invalid request")
		if not global_name[name] then
			global_name[name] = address
		end
		local message = pack_package("N", name, address)
		for k,v in pairs(slave_node) do
			socket.write(v.fd, message)
		end
	elseif t == 'Q' then
		-- query name
		local address = global_name[name]
		if address then
			socket.write(fd, pack_package("N", name, address))
		end
	else
		skynet.error("Invalid slave message type " .. t)
	end
end

local function monitor_slave(slave_id, slave_address)
	local fd = slave_node[slave_id].fd
	skynet.error(string.format("Harbor %d (fd=%d) report %s", slave_id, fd, slave_address))
	while pcall(dispatch_slave, fd) do end
	skynet.error("slave " ..slave_id .. " is down")
	local message = pack_package("D", slave_id)
	slave_node[slave_id].fd = 0
	for k,v in pairs(slave_node) do
		socket.write(v.fd, message)
	end
	socket.close(fd)
end

skynet.start(function()
	local master_addr = skynet.getenv "standalone"
	skynet.error("master listen socket " .. tostring(master_addr))
	local fd = socket.listen(master_addr)
	socket.start(fd , function(id, addr)
		skynet.error("connect from " .. addr .. " " .. id)
		socket.start(id)
		local ok, slave, slave_addr = pcall(handshake, id)
		if ok then
			skynet.fork(monitor_slave, slave, slave_addr)
		else
			skynet.error(string.format("disconnect fd = %d, error = %s", id, slave))
			socket.close(id)
		end
	end)
end)
