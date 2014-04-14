local skynet = require "skynet"
local db = {}

local command = {}

function command.GET(key)
	return db[key]
end

function command.SET(key, value)
	local last = db[key]
	db[key] = value
	return last
end

skynet.start(function()
	skynet.dispatch("lua", function(session, address, cmd, ...)
		local f = command[string.upper(cmd)]
		skynet.ret(skynet.pack(f(...)))
	end)
	skynet.register "SIMPLEDB"
end)
