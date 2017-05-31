local skynet = require "skynet"
require "skynet.manager"	-- import skynet.register

local command = {}

function command.RELOAD()
	local reload = require "reload"
	reload()
end

function command.SPEAK(...)
	print("FFFFFFFFFFFFFFFFFFFFF",...)
	return "UUU"
end

skynet.start(function()
	print("----------service1 start--------------")
	skynet.dispatch("lua", function(session, address, cmd, ...)
		local f = command[string.upper(cmd)]
		if f then
			skynet.ret(skynet.pack(f(...)))
		else
			error(string.format("Unknown command %s", tostring(cmd)))
		end
	end)
end)
