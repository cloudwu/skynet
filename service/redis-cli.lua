local skynet = require "skynet"
local socket = require "socket"
local string = string
local table = table
local tonumber = tonumber
local ipairs = ipairs
local unpack = unpack
local redis_server = ...

local function compose_message(msg)
	local lines = { "*" .. #msg }
	for _,v in ipairs(msg) do
		table.insert(lines,"$"..#v)
		table.insert(lines,v)
	end
	table.insert(lines,"")

	local cmd =  table.concat(lines,"\r\n")
	return cmd
end

local function init()
	socket.connect(redis_server)
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
	skynet.send(reply[2],reply[1],skynet.pack(...))
end

local redcmd = {}

redcmd[42] = function(data)	-- '*'
	local n = tonumber(data)
	if n < 1 then
		response(true, nil)
		return
	end
	local bulk = {}
	for i = 1,n do
		local line = socket.readline "\r\n"
		if line == nil then
			return "BLOCK"
		end
		local bytes = tonumber(string.sub(line,2) + 2)
		local data = socket.read(bytes)
		if data == nil then
			return "BLOCK"
		end
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
	local firstline = socket.read(bytes+2)
	if firstline == nil then
		return "BLOCK"
	end
	response(true,string.sub(firstline,1,-3))
end

redcmd[43] = function(data) -- '+'
	response(true,data)
end

redcmd[45] = function(data) -- '-'
	response(false,data)
end

redcmd[58] = function(data) -- ':'
	response(true, tonumber(data))
end

local function split_package()
	local result = socket.readline "\r\n"
	if result == nil then
		return
	end
	local firstchar = string.byte(result)
	local data = string.sub(result,2)
	local f = redcmd[firstchar]
	assert(f)
	if f(data) then
		return
	end
	socket.yield()
	return true
end

skynet.filter(
	function(session, address , msg, sz)
		if session == 0x7fffffff then
			socket.push(msg,sz)
			while split_package() do end
		elseif session < 0 then
			local message = { skynet.unpack(msg,sz) }
			local cmd = compose_message(message)
			socket.write(cmd)
			push_request_queue { -session , address }
		else
			return session, address, msg , sz
		end
	end
)

skynet.start(init)
