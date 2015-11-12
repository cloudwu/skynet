local skynet = require "skynet"
local socket = require "socket"
local socketchannel = require "socketchannel"

local table = table
local string = string
local assert = assert

local redis = {}
local command = {}
local meta = {
	__index = command,
	-- DO NOT close channel in __gc
}

---------- redis response
local redcmd = {}

redcmd[36] = function(fd, data) -- '$'
	local bytes = tonumber(data)
	if bytes < 0 then
		return true,nil
	end
	local firstline = fd:read(bytes+2)
	return true,string.sub(firstline,1,-3)
end

redcmd[43] = function(fd, data) -- '+'
	return true,data
end

redcmd[45] = function(fd, data) -- '-'
	return false,data
end

redcmd[58] = function(fd, data) -- ':'
	-- todo: return string later
	return true, tonumber(data)
end

local function read_response(fd)
	local result = fd:readline "\r\n"
	local firstchar = string.byte(result)
	local data = string.sub(result,2)
	return redcmd[firstchar](fd,data)
end

redcmd[42] = function(fd, data)	-- '*'
	local n = tonumber(data)
	if n < 0 then
		return true, nil
	end
	local bulk = {}
	local noerr = true
	for i = 1,n do
		local ok, v = read_response(fd)
		if ok then
			bulk[i] = v
		else
			noerr = false
		end
	end
	return noerr, bulk
end

-------------------

local function redis_login(auth, db)
	if auth == nil and db == nil then
		return
	end
	return function(so)
		if auth then
			so:request("AUTH "..auth.."\r\n", read_response)
		end
		if db then
			so:request("SELECT "..db.."\r\n", read_response)
		end
	end
end

function redis.connect(db_conf)
	local channel = socketchannel.channel {
		host = db_conf.host,
		port = db_conf.port or 6379,
		auth = redis_login(db_conf.auth, db_conf.db),
		nodelay = true,
	}
	-- try connect first only once
	channel:connect(true)
	return setmetatable( { channel }, meta )
end

function command:disconnect()
	self[1]:close()
	setmetatable(self, nil)
end

-- msg could be any type of value
local function pack_value(lines, v)
	if v == nil then
		return
	end

	v = tostring(v)

	table.insert(lines,"$"..#v)
	table.insert(lines,v)
end

local function compose_message(cmd, msg)
	local len = 1
	local t = type(msg)

	if t == "table" then
		len = len + #msg
	elseif t ~= nil then
		len = len + 1
	end

	local lines = {"*" .. len}
	pack_value(lines, cmd)

	if t == "table" then
		for _,v in ipairs(msg) do
			pack_value(lines, v)
		end
	else
		pack_value(lines, msg)
	end
	table.insert(lines, "")

	local chunk =  table.concat(lines,"\r\n")
	return chunk
end

setmetatable(command, { __index = function(t,k)
	local cmd = string.upper(k)
	local f = function (self, v, ...)
		if type(v) == "table" then
			return self[1]:request(compose_message(cmd, v), read_response)
		else
			return self[1]:request(compose_message(cmd, {v, ...}), read_response)
		end
	end
	t[k] = f
	return f
end})

local function read_boolean(so)
	local ok, result = read_response(so)
	return ok, result ~= 0
end

function command:exists(key)
	local fd = self[1]
	return fd:request(compose_message ("EXISTS", key), read_boolean)
end

function command:sismember(key, value)
	local fd = self[1]
	return fd:request(compose_message ("SISMEMBER", {key, value}), read_boolean)
end

function command:pipeline(ops)
	assert(ops and #ops > 0, "pipeline is null")

	local fd = self[1]

	local cmds = {}
	for _, cmd in ipairs(ops) do
		assert(#cmd >= 2, "pipeline error, the params length is less than 2")
		table.insert(cmds, compose_message(string.upper(table.remove(cmd, 1)), cmd))
	end

	return fd:request(table.concat(cmds, "\r\n"), function (fd)
		local result = {}
		for i=1, #ops do
			local ok, out = read_response(fd)
			table.insert(result, {ok = ok, out = out})
		end
		return true, result
	end)
end

--- watch mode

local watch = {}

local watchmeta = {
	__index = watch,
	__gc = function(self)
		self.__sock:close()
	end,
}

local function watch_login(obj, auth)
	return function(so)
		if auth then
			so:request("AUTH "..auth.."\r\n", read_response)
		end
		for k in pairs(obj.__psubscribe) do
			so:request(compose_message ("PSUBSCRIBE", k))
		end
		for k in pairs(obj.__subscribe) do
			so:request(compose_message("SUBSCRIBE", k))
		end
	end
end

function redis.watch(db_conf)
	local obj = {
		__subscribe = {},
		__psubscribe = {},
	}
	local channel = socketchannel.channel {
		host = db_conf.host,
		port = db_conf.port or 6379,
		auth = watch_login(obj, db_conf.auth),
		nodelay = true,
	}
	obj.__sock = channel

	-- try connect first only once
	channel:connect(true)
	return setmetatable( obj, watchmeta )
end

function watch:disconnect()
	self.__sock:close()
	setmetatable(self, nil)
end

local function watch_func( name )
	local NAME = string.upper(name)
	watch[name] = function(self, ...)
		local so = self.__sock
		for i = 1, select("#", ...) do
			local v = select(i, ...)
			so:request(compose_message(NAME, v))
		end
	end
end

watch_func "subscribe"
watch_func "psubscribe"
watch_func "unsubscribe"
watch_func "punsubscribe"

function watch:message()
	local so = self.__sock
	while true do
		local ret = so:response(read_response)
		local type , channel, data , data2 = ret[1], ret[2], ret[3], ret[4]
		if type == "message" then
			return data, channel
		elseif type == "pmessage" then
			return data2, data, channel
		elseif type == "subscribe" then
			self.__subscribe[channel] = true
		elseif type == "psubscribe" then
			self.__psubscribe[channel] = true
		elseif type == "unsubscribe" then
			self.__subscribe[channel] = nil
		elseif type == "punsubscribe" then
			self.__psubscribe[channel] = nil
		end
	end
end

return redis
