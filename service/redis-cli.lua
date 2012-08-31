local skynet = require "skynet"
local socket = require "socket"
local int64 = require "int64"
local string = string
local table = table
local tonumber = tonumber
local ipairs = ipairs
local unpack = unpack
local redis_server, redis_db = ...

local function compose_message(msg)
	local lines = { "*" .. #msg }
	for _,v in ipairs(msg) do
		local t = type(v)
		if t == "number" then
			v = tostring(v)
		elseif t == "userdata" then
			v = int64.tostring(int64.new(v),10)
		end
		table.insert(lines,"$"..#v)
		table.insert(lines,v)
	end
	table.insert(lines,"")

	local cmd =  table.concat(lines,"\r\n")
	return cmd
end

local function select_db(id)
	local result , ok = skynet.call(skynet.self(), "lua", "SELECT", tostring(id))
	assert(result and ok == "OK")
end

local request_queue = { head = 1, tail = 1 }

local function push_request_queue(reply)
	request_queue[request_queue.tail] = reply
	request_queue.tail = request_queue.tail + 1
end

local function pop_request_queue()
	assert(request_queue.head < request_queue.tail)
	local reply = request_queue[request_queue.head]
	request_queue[request_queue.head] = nil
	request_queue.head = request_queue.head + 1
	return reply
end

local function response(...)
	local reply = pop_request_queue()
	skynet.redirect(reply.address,0, "response", reply.session, skynet.pack(...))
end

local function readline(sep)
	while true do
		local line = socket.readline(sep)
		if line then
			return line
		end
		coroutine.yield()
	end
end

local function readbytes(bytes)
	while true do
		local block = socket.read(bytes)
		if block then
			return block
		end
		coroutine.yield()
	end
end

local redcmd = {}

redcmd[42] = function(data)	-- '*'
	local n = tonumber(data)
	if n < 0 then
		response(true, nil)
		return
	end
	local bulk = {}
	for i = 1,n do
		local line = readline "\r\n"
		local bytes = tonumber(string.sub(line,2) + 2)
		local data = readbytes(bytes)
		table.insert(bulk, string.sub(data,1,-3))
	end
	response(true, bulk)
end

redcmd[36] = function(data) -- '$'
	local bytes = tonumber(data)
	if bytes < 0 then
		response(true,nil)
		return
	end
	local firstline = readbytes(bytes+2)
	response(true,string.sub(firstline,1,-3))
end

redcmd[43] = function(data) -- '+'
	response(true,data)
end

redcmd[45] = function(data) -- '-'
	response(false,data)
end

redcmd[58] = function(data) -- ':'
	-- todo: return string later
	response(true, tonumber(data))
end

local function split_package()
	while true do
		local result = readline "\r\n"
		local firstchar = string.byte(result)
		local data = string.sub(result,2)
		local f = redcmd[firstchar]
		assert(f)
		f(data)
	end
end

local function init()
	while socket.connect(redis_server) do
		skynet.sleep(1000)
	end
	if redis_db then
		select_db(redis_db)
	end
end

local split_co = coroutine.create(split_package)

local function reconnect()
	init()
	for i = request_queue.head, request_queue.tail-1 do
		local request = request_queue[i]
		socket.write(request[3])
	end
	split_co = coroutine.create(split_package)
end

skynet.register_protocol {
	name = "client",
	id = 3,
	pack = function(...) return ... end,
	unpack = function(msg,sz)
		if msg == nil then
			skynet.timeout(0, reconnect)
			return
		end
		socket.push(msg,sz)
		assert(coroutine.resume(split_co))
	end,
	dispatch = function () end
}

skynet.start(function()
	skynet.dispatch("lua", function(session, address, ...)
		local message = { ... }
		local cmd = compose_message(message)
		socket.write(cmd)
		push_request_queue { session = session , address = address, cmd = cmd }
	end)
	init()
end)

