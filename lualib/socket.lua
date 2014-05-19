local driver = require "socketdriver"
local skynet = require "skynet"
local assert = assert

local socket = {}	-- api
local buffer_pool = {}	-- store all message buffer object
local socket_pool = setmetatable( -- store all socket object
	{},
	{ __gc = function(p)
		for id,v in pairs(p) do
			driver.close(id)
			-- don't need clear v.buffer, because buffer pool will be free at the end
			p[id] = nil
		end
	end
	}
)

local socket_message = {}

local function wakeup(s)
	local co = s.co
	if co then
		s.co = nil
		skynet.wakeup(co)
	end
end

local function suspend(s)
	assert(not s.co)
	s.co = coroutine.running()
	skynet.wait()
	-- wakeup closing corouting every time suspend,
	-- because socket.close() will wait last socket buffer operation before clear the buffer.
	if s.closing then
		skynet.wakeup(s.closing)
	end
end

-- read skynet_socket.h for these macro
-- SKYNET_SOCKET_TYPE_DATA = 1
socket_message[1] = function(id, size, data)
	local s = socket_pool[id]
	if s == nil then
		skynet.error("socket: drop package from " .. id)
		driver.drop(data, size)
		return
	end

	local sz = driver.push(s.buffer, buffer_pool, data, size)
	local rr = s.read_required
	local rrt = type(rr)
	if rrt == "number" then
		-- read size
		if sz >= rr then
			s.read_required = nil
			wakeup(s)
		end
	elseif rrt == "string" then
		-- read line
		if driver.readline(s.buffer,nil,rr) then
			s.read_required = nil
			wakeup(s)
		end
	end
end

-- SKYNET_SOCKET_TYPE_CONNECT = 2
socket_message[2] = function(id, _ , addr)
	local s = socket_pool[id]
	if s == nil then
		return
	end
	-- log remote addr
	s.connected = true
	wakeup(s)
end

-- SKYNET_SOCKET_TYPE_CLOSE = 3
socket_message[3] = function(id)
	local s = socket_pool[id]
	if s == nil then
		return
	end
	s.connected = false
	wakeup(s)
end

-- SKYNET_SOCKET_TYPE_ACCEPT = 4
socket_message[4] = function(id, newid, addr)
	local s = socket_pool[id]
	if s == nil then
		driver.close(newid)
		return
	end
	s.callback(newid, addr)
end

-- SKYNET_SOCKET_TYPE_ERROR = 5
socket_message[5] = function(id)
	local s = socket_pool[id]
	if s == nil then
		skynet.error("socket: error on unknown", id)
		return
	end
	if s.connected then
		skynet.error("socket: error on", id)
	end
	s.connected = false

	wakeup(s)
end

skynet.register_protocol {
	name = "socket",
	id = skynet.PTYPE_SOCKET,	-- PTYPE_SOCKET = 6
	unpack = driver.unpack,
	dispatch = function (_, _, t, n1, n2, data)
		socket_message[t](n1,n2,data)
	end
}

local function connect(id, func)
	local newbuffer
	if func == nil then
		newbuffer = driver.buffer()
	end
	local s = {
		id = id,
		buffer = newbuffer,
		connected = false,
		read_require = false,
		co = false,
		callback = func,
	}
	socket_pool[id] = s
	suspend(s)
	if s.connected then
		return id
	end
end

function socket.open(addr, port)
	local id = driver.connect(addr,port)
	return connect(id)
end

function socket.bind(os_fd)
	local id = driver.bind(os_fd)
	return connect(id)
end

function socket.stdin()
	return socket.bind(0)
end

function socket.start(id, func)
	driver.start(id)
	return connect(id, func)
end

function socket.shutdown(id)
	local s = socket_pool[id]
	if s then
		if s.buffer then
			driver.clear(s.buffer,buffer_pool)
		end
		if s.connected then
			driver.close(id)
		end
	end
end

function socket.close(id)
	local s = socket_pool[id]
	if s == nil then
		return
	end
	if s.connected then
		driver.close(s.id)
		-- notice: call socket.close in __gc should be carefully,
		-- because skynet.wait never return in __gc, so driver.clear may not be called
		if s.co then
			-- reading this socket on another coroutine, so don't shutdown (clear the buffer) immediatel
			-- wait reading coroutine read the buffer.
			assert(not s.closing)
			s.closing = coroutine.running()
			skynet.wait()
		else
			suspend(s)
		end
		s.connected = false
	end
	socket.shutdown(id)
	assert(s.lock_set == nil or next(s.lock_set) == nil)
	socket_pool[id] = nil
end

function socket.read(id, sz)
	local s = socket_pool[id]
	assert(s)
	local ret = driver.pop(s.buffer, buffer_pool, sz)
	if ret then
		return ret
	end
	if not s.connected then
		return false, driver.readall(s.buffer, buffer_pool)
	end

	assert(not s.read_required)
	s.read_required = sz
	suspend(s)
	ret = driver.pop(s.buffer, buffer_pool, sz)
	if ret then
		return ret
	else
		return false, driver.readall(s.buffer, buffer_pool)
	end
end

function socket.readall(id)
	local s = socket_pool[id]
	assert(s)
	if not s.connected then
		local r = driver.readall(s.buffer, buffer_pool)
		return r ~= "" and r
	end
	assert(not s.read_required)
	s.read_required = true
	suspend(s)
	assert(s.connected == false)
	return driver.readall(s.buffer, buffer_pool)
end

function socket.readline(id, sep)
	sep = sep or "\n"
	local s = socket_pool[id]
	assert(s)
	local ret = driver.readline(s.buffer, buffer_pool, sep)
	if ret then
		return ret
	end
	if not s.connected then
		return false, driver.readall(s.buffer, buffer_pool)
	end
	assert(not s.read_required)
	s.read_required = sep
	suspend(s)
	if s.connected then
		return driver.readline(s.buffer, buffer_pool, sep)
	else
		return false, driver.readall(s.buffer, buffer_pool)
	end
end

function socket.block(id)
	local s = socket_pool[id]
	if not s or not s.connected then
		return false
	end
	assert(not s.read_required)
	s.read_required = 0
	suspend(s)
	return s.connected
end

socket.write = assert(driver.send)
socket.lwrite = assert(driver.lsend)

function socket.invalid(id)
	return socket_pool[id] == nil
end

socket.listen = assert(driver.listen)

function socket.lock(id)
	local s = socket_pool[id]
	assert(s)
	local lock_set = s.lock
	if not lock_set then
		lock_set = {}
		s.lock = lock_set
	end
	if #lock_set == 0 then
		lock_set[1] = true
	else
		local co = coroutine.running()
		table.insert(lock_set, co)
		skynet.wait()
	end
end

function socket.unlock(id)
	local s = socket_pool[id]
	assert(s)
	local lock_set = assert(s.lock)
	table.remove(lock_set,1)
	local co = lock_set[1]
	if co then
		skynet.wakeup(co)
	end
end

-- abandon use to forward socket id to other service
-- you must call socket.start(id) later in other service
function socket.abandon(id)
	local s = socket_pool[id]
	if s and s.buffer then
		driver.clear(s.buffer,buffer_pool)
	end
	socket_pool[id] = nil
end

return socket
