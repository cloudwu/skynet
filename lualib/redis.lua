local skynet = require "skynet"
local string = string
local table =  table
local unpack = unpack

local redis = {}

local command = {}

redis.cmd = command

local function assert_redis(result, err, ...)
	assert(result, err)
	return err, ...
end

setmetatable(command, { __index = function(t,k)
	local f = function(...)
		return assert_redis(skynet.call(".redis", skynet.unpack, skynet.pack(k, ...)))
	end
	t[k]  = f
	return f
end})

function command.EXISTS(key)
	local result , exists = skynet.call(".redis", skynet.unpack, skynet.pack("EXISTS", key))
	assert(result, exists)
	exists = exists ~= 0
	return exists
end

local function split(cmd)
	local cmds = {}
	for v in string.gmatch(cmd,"[^ ]+") do
		table.insert(cmds,v)
	end
	return unpack(cmds)
end

local function send_command(cmd, ...)
	return command[cmd](...)
end


function redis.send(cmd, more, ...)
	if more == nil then
		send_command(split(cmd))
	else
		send_command(cmd, more, ...)
	end
end

return redis
