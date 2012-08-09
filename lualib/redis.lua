local skynet = require "skynet"
local string = string
local table =  table
local unpack = unpack

local redis = {}

local command = {}

redis.cmd = command

setmetatable(command, { __index = function(t,k)
	local f = function(...)
		return skynet.call(".redis", skynet.unpack, skynet.pack(k, ...))
	end
	t[k]  = f
	return f
end})

function command.EXISTS(key)
	local result , exists = skynet.call(".redis", skynet.unpack, skynet.pack("EXISTS", key))
	exists = exists ~= 0
	return result, exists
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
