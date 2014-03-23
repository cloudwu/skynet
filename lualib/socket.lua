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
end

-- read skynet_socket.h for these macro
-- SKYNET_SOCKET_TYPE_DATA = 1
socket_message[1] = function(id, size, data)
	local s = socket_pool[id]
	if s == nil then
		print("socket: drop package from " .. id)
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
		print("socket: error on unknown", id)
		return
	end
	if s.connected then
		print("socket: error on", id)
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

function socket.stdin()
	local id = driver.bind(1)
	return connect(id)
end

function socket.start(id, func)
	driver.start(id)
	return connect(id, func)
end

local function clear_socket(id)
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
			-- reading this socket on another coroutine
			assert(not s.closing)
			s.closing = coroutine.running()
			skynet.wait()
		else
			suspend(s)
		end
		s.connected = false
	end
	clear_socket(id)
	assert(s.lock_set == nil or next(s.lock_set) == nil)
	socket_pool[id] = nil
end

local function close_socket(s)
	if s.closing then
		skynet.wakeup(s.closing)
	end
	return driver.readall(s.buffer, buffer_pool)
end

function socket.read(id, sz)
	local s = socket_pool[id]
	assert(s)
	local ret = driver.pop(s.buffer, buffer_pool, sz)
	if ret then
		return ret
	end
	if not s.connected then
		return false, close_socket(s)
	end

	assert(not s.read_required)
	s.read_required = sz
	suspend(s)
	ret = driver.pop(s.buffer, buffer_pool, sz)
	if ret then
		return ret
	else
		return false, close_socket(s)
	end
end

function socket.readall(id)
	local s = socket_pool[id]
	assert(s)
	if not s.connected then
		local r = close_socket(s)
		return r ~= "" and r
	end
	assert(not s.read_required)
	s.read_required = true
	suspend(s)
	assert(s.connected == false)
	return close_socket(s)
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
		return false, close_socket(s)
	end
	assert(not s.read_required)
	s.read_required = sep
	suspend(s)
	if s.connected then
		return driver.readline(s.buffer, buffer_pool, sep)
	else
		return false, close_socket(s)
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

-- channel support auto reconnect , and capture socket error in request/response transaction
-- { host = "", port = , auth = function(so) , response = function(so) session, data }

local channel = {}
local channel_socket = {}
local channel_meta = { __index = channel }
local channel_socket_meta = {
	__index = channel_socket,
	__gc = function(cs)
		local fd = cs[1]
		cs[1] = false
		if fd then
			clear_socket(fd)
		end
	end
}
local socket_error = channel_socket	-- alias for error object

function socket.channel(desc)
	local c = {
		__host = assert(desc.host),
		__port = assert(desc.port),
		__auth = desc.auth,
		__response = desc.response,
		__request = {},	-- request seq { response func or session }
		__thread = {}, -- coroutine seq or session->coroutine map
		__result = {}, -- response result { coroutine -> result }
		__result_data = {},
		__connecting = {},
		__sock = false,
		__closed = false,
	}

	return setmetatable(c, channel_meta)
end

local function close_channel_socket(self)
	if self.__sock then
		local so = self.__sock
		self.__sock = false
		socket.close(so[1])
	end
end

local function wakeup_all(self, errmsg)
	for i = 1, #self.__thread do
		local co = self.__thread[i]
		self.__thread[i] = nil
		self.__result[co] = socket_error
		self.__result_data[co] = errmsg
		skynet.wakeup(co)
	end
end

local function dispatch_response(self)
	local response = self.__response
	if response then
		-- response() return session
		while self.__sock do
			local ok , session, result_ok, result_data = pcall(response, self.__sock)
			if ok and session then
				local co = self.__thread[session]
				self.__thread[session] = nil
				if co then
					self.__result[co] = result_ok
					self.__result_data[co] = result_data
					skynet.wakeup(co)
				else
					print("socket: unknown session :", session)
				end
			else
				close_channel_socket(self)
				local errormsg
				if session ~= socket_error then
					errormsg = session
				end
				for k,co in pairs(self.__thread) do
					-- throw error (errormsg)
					self.__thread[k] = nil
					self.__result[co] = socket_error
					self.__result_data[co] = errormsg
					skynet.wakeup(co)
				end
			end
		end
	else
		-- pop response function from __request
		while self.__sock do
			local func = table.remove(self.__request, 1)
			if func == nil then
				if not socket.block(self.__sock[1]) then
					close_channel_socket(self)
					wakeup_all(self)
				end
			else
				local ok, result_ok, result_data = pcall(func, self.__sock)
				if ok then
					local co = table.remove(self.__thread, 1)
					self.__result[co] = result_ok
					self.__result_data[co] = result_data
					skynet.wakeup(co)
				else
					close_channel_socket(self)
					local errmsg
					if result ~= socket_error then
						errmsg = result_ok
					end
					wakeup_all(self, errmsg)
				end
			end
		end
	end
end

local function try_connect(self)
	assert(not self.__sock)
	local fd = socket.open(self.__host, self.__port)
	if not fd then
		return
	end
	self.__sock = setmetatable( {fd} , channel_socket_meta )
	skynet.fork(dispatch_response, self)

	if self.__auth then
		local ok , message = pcall(self.__auth, self)
		if not ok then
			close_channel_socket(self)
			if message ~= socket_error then
				error(message)
			end
		end
	end
end

function channel:connect()
	if self.__sock then
		return true
	end
	if self.__closed then
		self.__closed = false
	end
	if #self.__connecting > 0 then
		-- connecting in other coroutine
		local co = coroutine.running()
		table.insert(self.__connecting, co)
		skynet.wait()
	else
		self.__connecting[1] = true
		try_connect(self)
		self.__connecting[1] = nil
		for i=2, #self.__connecting do
			local co = self.__connecting[i]
			self.__connecting[i] = nil
			skynet.wakeup(co)
		end
	end

	if self.__sock then
		return true
	else
		return false
	end
end

local function reconnect_channel(self)
	local t = 100
	while not self.__closed do
		if self:connect() then
			return
		end
		if t > 1000 then
			print("socket: try to reconnect", self.__host, self.__port)
			skynet.sleep(t)
			t = 0
		else
			skynet.sleep(t)
		end
		t = t + 100
	end
end

function channel:request(request, response)
	if not self.__sock then
		assert(not self.__closed)
		reconnect_channel(self)
	end

	if not socket.write(self.__sock[1], request) then
		return self:request(request, response)
	end

	if response == nil then
		-- no response
		return
	end

	local co = coroutine.running()

	if self.__response then
		-- response is session
		self.__thread[response] = co
	else
		-- response is a function, push it to __request
		table.insert(self.__request, response)
		table.insert(self.__thread, co)
	end
	skynet.wait()

	local result = self.__result[co]
	self.__result[co] = nil
	local result_data = self.__result_data[co]
	self.__result_data[co] = nil

	if result == socket_error then
		if result_data then
			print("socket: dispatch", request, result_data)
		end
		return self:request(request, response)
	else
		assert(result, result_data)
		return result_data
	end
end

function channel:response(response)
	if not self.__sock then
		assert(not self.__closed)
		reconnect_channel(self)
	end
	assert(type(response) == "function")

	local co = coroutine.running()
	table.insert(self.__request, response)
	table.insert(self.__thread, co)

	skynet.wait()

	local result = self.__result[co]
	self.__result[co] = nil
	local result_data = self.__result_data[co]
	self.__result_data[co] = nil

	if result == socket_error then
		if result_data then
			print("socket: dispatch", request, result_data)
		end
		return self:response(response)
	else
		assert(result, result_data)
		return result_data
	end
end

function channel:close()
	if not self.__closed then
		self.__closed = true
		close_channel_socket(self)
	end
end

channel_meta.__gc = channel.close

local function wrapper_socket_function(f)
	return function(self, ...)
		local result = f(self[1], ...)
		if not result then
			error(socket_error)
		else
			return result
		end
	end
end

channel_socket.read = wrapper_socket_function(socket.read)
channel_socket.readline = wrapper_socket_function(socket.readline)

return socket
