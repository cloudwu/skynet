local skynet = require "skynet"
local string = string
local table =  table
local unpack = unpack
local assert = assert

local redis_manager
local batch = false

local redis = {}

local command = {}

setmetatable(command, { __index = function(t,k)
	local cmd = string.upper(k)
	local f = function(self, ...)
		if batch then
			skynet.send( self.__handle, "lua", cmd , ...)
		else
			local err, result = skynet.call( self.__handle, "lua", cmd, ...)
			assert(err, result)
			return result
		end
	end
	t[k]  = f
	return f
end})

function command:exists(key)
	assert(not batch, "exists can't used in batch mode")
	local result , exists = skynet.call( self.__handle, "lua" , "EXISTS", key)
	assert(result, exists)
	exists = exists ~= 0
	return exists
end

function command:batch(mode)
	if mode == "end" then
		assert(batch, "Open batch mode first")
		batch = false
		local err, result = skynet.unpack(skynet.rawcall( self.__handle, "text", mode))
		assert(err, result)
		return result
	else
		assert(mode == "read" or mode == "write")
		batch = mode
		skynet.send( self.__handle, "text", mode)
	end
end

local meta = {
	__index = command
}

function redis.connect(dbname)
	local handle = skynet.call(redis_manager, "lua", dbname)
	assert(handle ~= nil)
	return setmetatable({ __handle = handle } , meta)
end

skynet.init(function()
	redis_manager = skynet.uniqueservice("redis-mgr")
end, "redis")

return redis
