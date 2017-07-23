local skynet = require "skynet"

local service_name = (...)
local init = {}

function init.init(code, ...)
	skynet.dispatch("lua", function() error("No dispatch function")	end)
	skynet.start = function(f) f()	end
	local mainfunc = assert(load(code, service_name))
	mainfunc(...)
	skynet.ret()
end

skynet.start(function()
	skynet.dispatch("lua", function(_,_,cmd,...)
		init[cmd](...)
	end)
end)
