local skynet = require "skynet"

local service = {}

local function query(service_name, ...)
	local s = service[service_name]
	if s == nil then
		service[service_name] = false
		s = skynet.newservice(service_name, ...)
		service[service_name] = s
	end
	return s
end

skynet.start(function()
	skynet.dispatch("lua", function(session, address, service_name , ...)
		local handle = query(service_name, ...)
		skynet.ret(skynet.pack(handle))
	end)
	skynet.register(".service")
	if skynet.getenv "standalone" then
		skynet.register("SERVICE")
	end
end)
