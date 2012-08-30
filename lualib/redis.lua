local skynet = require "skynet"
local string = string
local table =  table
local unpack = unpack

local redis = {}

local command = {}

setmetatable(command, { __index = function(t,k)
	local cmd = string.upper(k)
	local f = function(self, ...)
		local err, result = skynet.call( self.__handle, skynet.unpack, skynet.pack(cmd, ...))
		assert(err, result)
		return result
	end
	t[k]  = f
	return f
end})

function command:exists(key)
	local result , exists = skynet.call( self.__handle, skynet.unpack, skynet.pack("EXISTS", key))
	assert(result, exists)
	exists = exists ~= 0
	return exists
end

local meta = {
	__index = command
}

function redis.connect(dbname)
	local handle = skynet.call(".redis-manager",skynet.unpack, skynet.pack(dbname))
	assert(handle ~= nil)
	return setmetatable({ __handle = handle } , meta)
end

return redis
