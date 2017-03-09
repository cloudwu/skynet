local skynet = require "skynet"
local reload = require "reload"

local arg = ...

A = { tostring(arg) }
setmetatable(A , { __gc = function(t) skynet.error("Version gone :" .. t[1]) end })


skynet.start(function()
	local version = tonumber(arg) or 0
	skynet.error(string.format("Version %d", version))
	skynet.dispatch("lua", function(_,_,msg)
		skynet.sleep(100)
		skynet.ret(skynet.pack(msg))
	end)
	skynet.timeout(450, function()
		reload(version + 1)
	end)
end)
