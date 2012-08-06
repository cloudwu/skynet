local skynet = require "skynet"
local db = {}

local command = {}

function command.GET(key)
	skynet.ret(db[key])
end

function command.SET(key, value)
	local last = db[key]
	db[key] = value
	skynet.ret(last)
end

skynet.dispatch(function(message, from, session)
	print("simpledb",message, from, session)
	local cmd, key , value = string.match(message, "(%w+) (%w+) ?(.*)")
	command[cmd](key,value)
end)

skynet.register "SIMPLEDB"
