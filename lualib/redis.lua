local skynet = require "skynet"
local socket = require "socket"
local config = require "config"
local redis_conf = skynet.getenv "redis"
local name = config (redis_conf)

local readline = socket.readline
local readbytes = socket.read
local table = table
local string = string

local redis = {}
local command = {}
local meta = {
	__index = command,
	__gc = function(self)
		socket.close(self.__handle)
	end,
}

function redis.connect(dbname)
	local db_conf   =   name[dbname]
	local fd = assert(socket.open(db_conf.host, db_conf.port or 6379))
	local r = setmetatable( { __handle = fd, __mode = false }, meta )
	if db_conf.auth ~= nil then
		r:auth(db_conf.auth)
	end
	if db_conf.db ~= nil then
		r:select(db_conf.db)
	end

	return r
end

function command:disconnect()
	socket.close(self.__handle)
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

local redcmd = {}

redcmd[42] = function(fd, data)	-- '*'
	local n = tonumber(data)
	if n < 0 then
		return true, nil
	end
	local bulk = {}
	for i = 1,n do
		local line = readline(fd,"\r\n")
		local bytes = tonumber(string.sub(line,2))
		if bytes >= 0 then
			local data = readbytes(fd, bytes + 2)
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
	local firstline = readbytes(fd, bytes+2)
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
	local result = readline(fd, "\r\n")
	local firstchar = string.byte(result)
	local data = string.sub(result,2)
	return redcmd[firstchar](fd,data)
end

setmetatable(command, { __index = function(t,k)
	local cmd = string.upper(k)
	local f = function (self, ...)
		local fd = self.__handle
		if self.__mode then
			socket.write(fd, compose_message { cmd, ... })
			self.__batch = self.__batch + 1
		else
			socket.lock(fd)
			socket.write(fd, compose_message { cmd, ... })
			local ok, ret = read_response(fd)
			socket.unlock(fd)
			assert(ok, ret)
			return ret
		end
	end
	t[k] = f
	return f
end})

function command:exists(key)
	assert(not self.__mode, "exists can't used in batch mode")
	local fd = self.__handle
	socket.lock(fd)
	socket.write(fd, compose_message { "EXISTS", key })
	local ok, exists = read_response(fd)
	socket.unlock(fd)
	assert(ok, exists)
	return exists ~= 0
end

function command:sismember(key, value)
	assert(not self.__mode, "sismember can't used in batch mode")
	local fd = self.__handle
	socket.lock(fd)
	socket.write(fd, compose_message { "SISMEMBER", key, value })
	local ok, ismember = read_response(fd)
	socket.unlock(fd)
	assert(ok, ismember)
	return ismember ~= 0
end

function command:batch(mode)
	if mode == "end" then
		local fd = self.__handle
		if self.__mode == "read" then
			local allok = true
			local allret = {}
			for i = 1, self.__batch do
				local ok, ret = read_response(fd)
				allok = allok and ok
				allret[i] = ret
			end
			self.__mode = false
			socket.unlock(self.__handle)
			assert(allok, "batch read failed")
			return allret
		else
			local allok = true
			for i = 1, self.__batch do
				local ok = read_response(fd)
				allok = allok and ok
			end
			self.__mode = false
			socket.unlock(self.__handle)
			return allok
		end
	else
		assert(mode == "read" or mode == "write")
		socket.lock(self.__handle)
		self.__mode = mode
		self.__batch = 0
	end
end

function command:multi()
	local fd = self.__handle
	socket.lock(fd)
	self.__mode = "multi"
	self.__batch = 0
	socket.write(fd, "MULTI\r\n")
end

local function read_exec(fd)
	local result = readline(fd, "\r\n")
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
		result[i] = r
		if err then
			err[i] = ok
		else
			if ok == false then
				err = {}
				for j = 1, i-1 do
					err[j] = true
				end
				err[i] = false
			end
		end
	end
	return result, err
end

function command:exec()
	if self.__mode ~= "multi" then
		error "call multi first"
	end
	local fd = self.__handle
	socket.write(fd, "EXEC\r\n")
	local allok = true
	for i = 0, self.__batch do
		local ok, queue = read_response(fd)
		allok = allok and ok
	end
	if not allok then
		self.__mode = false
		socket.unlock(fd)
		error "Queue command error"
	end

	local result, err = read_exec(fd)

	self.__mode = false
	socket.unlock(fd)

	if not result then
		error(err)
	elseif err then
		local errmsg = ""
		for k,v in ipairs(err) do
			if v == false then
				errmsg = errmsg .. k .. ":" .. result[k]
			end
		end
		error(errmsg)
	end

	return result
end

return redis
