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

skynet.start(function()
	skynet.dispatch("text", function(session, address, message)
		print("simpledb",message, skynet.address(address), session)
		local cmd, key , value = string.match(message, "(%w+) (%w+) ?(.*)")
		local f = command[cmd]
		if f then
			f(key,value)
		else
			skynet.ret("Invalid command : "..message)
		end
	end)
	skynet.register "SIMPLEDB"
end)


