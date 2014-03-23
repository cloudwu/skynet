local skynet = require "skynet"
local socket = require "socket"
local config = require "config"
local redis_conf = skynet.getenv "redis"
local name = config (redis_conf)

local table = table
local string = string

local redis = {}
local command = {}
local meta = {
	__index = command,
	__gc = function(self)
		self[1]:close()
	end,
}

---------- redis response
local redcmd = {}

redcmd[42] = function(fd, data)	-- '*'
	local n = tonumber(data)
	if n < 0 then
		return true, nil
	end
	local bulk = {}
	for i = 1,n do
		local line = fd:readline "\r\n"
		local bytes = tonumber(string.sub(line,2))
		if bytes >= 0 then
			local data = fd:read(bytes + 2)
			-- bulk[i] = nil when bytes < 0
			bulk[i] = string.sub(data,1,-3)
		end
	end
	return true, bulk
end

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

function redis.connect(dbname)
	local db_conf = name[dbname]
	local channel = socket.channel {
		host = db_conf.host,
		port = db_conf.port or 6379,
		auth = redis_login(db_conf.auth, db_conf.db),
	}
	return setmetatable( { channel }, meta )
end

function command:disconnect()
	self[1]:close()
	setmetatable(self, nil)
end

local function compose_message(msg)
	if #msg == 1 then
		return msg[1] .. "\r\n"
	end
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

setmetatable(command, { __index = function(t,k)
	local cmd = string.upper(k)
	local f = function (self, ...)
		return self[1]:request(compose_message { cmd, ... }, read_response)
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
	return fd:request(compose_message { "EXISTS", key }, read_boolean)
end

function command:sismember(key, value)
	local fd = self[1]
	return fd:request(compose_message { "SISMEMBER", key, value }, read_boolean)
end

local function read_exec(fd)
	local result = fd:readline "\r\n"
	local firstchar = string.byte(result)
	local data = string.sub(result,2)
	if firstchar ~= 42 then
		return false, data
	end

	local n = tonumber(data)
	local result = {}
	local err = nil
	for i = 1,n do
		local ok, r = read_response(fd)
		if ok then
			result[i] = r
		else
			if not err then
				err = {}
			end
			table.insert(err, i .. ":" .. r)
		end
	end
	if err then
		return false, table.concat(err,",")
	else
		return true, result
	end
end

function command:exec()
	return self[1]:request( "EXEC\r\n" , read_exec)
end

return redis
