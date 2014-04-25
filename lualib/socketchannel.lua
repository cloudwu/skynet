local skynet = require "skynet"
local socket = require "socket"

-- channel support auto reconnect , and capture socket error in request/response transaction
-- { host = "", port = , auth = function(so) , response = function(so) session, data }

local socket_channel = {}
local channel = {}
local channel_socket = {}
local channel_meta = { __index = channel }
local channel_socket_meta = {
	__index = channel_socket,
	__gc = function(cs)
		local fd = cs[1]
		cs[1] = false
		if fd then
			socket.shutdown(fd)
		end
	end
}

local socket_error = setmetatable({}, {__tostring = function() return "[Error: socket]" end })	-- alias for error object
socket_channel.error = socket_error

function socket_channel.channel(desc)
	local c = {
		__host = assert(desc.host),
		__port = assert(desc.port),
		__auth = desc.auth,
		__response = desc.response,	-- It's for session mode
		__request = {},	-- request seq { response func or session }	-- It's for order mode
		__thread = {}, -- coroutine seq or session->coroutine map
		__result = {}, -- response result { coroutine -> result }
		__result_data = {},
		__connecting = {},
		__sock = false,
		__closed = false,
		__authcoroutine = false,
	}

	return setmetatable(c, channel_meta)
end

local function close_channel_socket(self)
	if self.__sock then
		local so = self.__sock
		self.__sock = false
		-- never raise error
		pcall(socket.close,so[1])
	end
end

local function wakeup_all(self, errmsg)
	if self.__response then
		for k,co in pairs(self.__thread) do
			self.__thread[k] = nil
			self.__result[co] = socket_error
			self.__result_data[co] = errmsg
			skynet.wakeup(co)
		end
	else
		for i = 1, #self.__request do
			self.__request[i] = nil
		end
		for i = 1, #self.__thread do
			local co = self.__thread[i]
			self.__thread[i] = nil
			self.__result[co] = socket_error
			self.__result_data[co] = errmsg
			skynet.wakeup(co)
		end
	end
end



local function dispatch_by_session(self)
	local response = self.__response
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
				skynet.error("socket: unknown session :", session)
			end
		else
			close_channel_socket(self)
			local errormsg
			if session ~= socket_error then
				errormsg = session
			end
			wakeup_all(self, errormsg)
		end
	end
end

local function pop_response(self)
	return table.remove(self.__request, 1), table.remove(self.__thread, 1)
end

local function push_response(self, response, co)
	if self.__response then
		-- response is session
		self.__thread[response] = co
	else
		-- response is a function, push it to __request
		table.insert(self.__request, response)
		table.insert(self.__thread, co)
	end
end

local function dispatch_by_order(self)
	while self.__sock do
		local func, co = pop_response(self)
		if func == nil then
			if not socket.block(self.__sock[1]) then
				close_channel_socket(self)
				wakeup_all(self)
			end
		else
			local ok, result_ok, result_data = pcall(func, self.__sock)
			if ok then
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

local function dispatch_function(self)
	if self.__response then
		return dispatch_by_session
	else
		return dispatch_by_order
	end
end

local function connect_once(self)
	assert(not self.__sock and not self.__authcoroutine)
	local fd = socket.open(self.__host, self.__port)
	if not fd then
		return false
	end
	self.__authcoroutine = coroutine.running()
	self.__sock = setmetatable( {fd} , channel_socket_meta )
	skynet.fork(dispatch_function(self), self)

	if self.__auth then
		local ok , message = pcall(self.__auth, self)
		if not ok then
			close_channel_socket(self)
			if message ~= socket_error then
				skynet.error("socket: auth failed", message)
			end
		end
		self.__authcoroutine = false
		return ok
	end

	self.__authcoroutine = false
	return true
end

local function try_connect(self , once)
	local t = 100
	while not self.__closed do
		if connect_once(self) then
			if not once then
				skynet.error("socket: connect to", self.__host, self.__port)
			end
			return
		elseif once then
			error(string.format("Connect to %s:%d failed", self.__host, self.__port))
		end
		if t > 1000 then
			skynet.error("socket: try to reconnect", self.__host, self.__port)
			skynet.sleep(t)
			t = 0
		else
			skynet.sleep(t)
		end
		t = t + 100
	end
end

local function block_connect(self, once)
	if self.__sock then
		local authco = self.__authcoroutine
		if not authco then
			return true
		end
		if authco == coroutine.running() then
			-- authing
			return true
		end
	end
	if self.__closed then
		return false
	end

	if #self.__connecting > 0 then
		-- connecting in other coroutine
		local co = coroutine.running()
		table.insert(self.__connecting, co)
		skynet.wait()
		-- check connection again
		return block_connect(self, once)
	end
	self.__connecting[1] = true
	try_connect(self, once)
	self.__connecting[1] = nil
	for i=2, #self.__connecting do
		local co = self.__connecting[i]
		self.__connecting[i] = nil
		skynet.wakeup(co)
	end

	-- check again
	return block_connect(self, once)
end

function channel:connect(once)
	if self.__closed then
		self.__closed = false
	end

	return block_connect(self, once)
end

local function wait_for_response(self, response)
	local co = coroutine.running()
	push_response(self, response, co)
	skynet.wait()

	local result = self.__result[co]
	self.__result[co] = nil
	local result_data = self.__result_data[co]
	self.__result_data[co] = nil

	if result == socket_error then
		error(socket_error)
	else
		assert(result, result_data)
		return result_data
	end
end

function channel:request(request, response)
	assert(block_connect(self))

	if not socket.write(self.__sock[1], request) then
		close_channel_socket(self)
		wakeup_all(self)
		error(socket_error)
	end

	if response == nil then
		-- no response
		return
	end

	return wait_for_response(self, response)
end

function channel:response(response)
	assert(block_connect(self))

	return wait_for_response(self, response)
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

return socket_channel
