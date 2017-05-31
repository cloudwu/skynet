local skynet = require "skynet"
require "skynet.manager"	-- import skynet.register

function set_timeout(ti, f)
	local function t()
		if f then
			f()
		end
	end
 	skynet.timeout(ti, t)
 	return function() f=nil end
end

function force_fork(f)
	local function func()
		if f then
			f()
		end
	end
 	skynet.fork(func)
 	return function() f=nil end
end

skynet.start(function()
	print("----------service1 start--------------")
	skynet.dispatch("lua", function(session, address, data, data1, data2)
		skynet.retpack("service2")
	end)
end)
