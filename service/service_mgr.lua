local skynet = require "skynet"

local cmd = {}
local service = {}

function cmd.LAUNCH(service_name, ...)
	local s = service[service_name]
	if type(s) == "number" then
		return s
	end

	if s == nil then
		s = { launch = true }
		service[service_name] = s
	elseif s.launch then
		assert(type(s) == "table")
		local co = coroutine.running()
		table.insert(s, co)
		skynet.wait()
		s = service[service_name]
		assert(type(s) == "number")
		return s
	end

	local handle = skynet.newservice(service_name, ...)
	for _,v in ipairs(s) do
		skynet.wakeup(v)
	end

	service[service_name] = handle

	return handle
end

function cmd.QUERY(service_name)
	local s = service[service_name]
	if type(s) == "number" then
		return s
	end
	if s == nil then
		s = {}
		service[service_name] = s
	end
	assert(type(s) == "table")
	local co = coroutine.running()
	table.insert(s, co)
	skynet.wait()
	s = service[service_name]
	assert(type(s) == "number")
	return s
end

skynet.start(function()
	skynet.dispatch("lua", function(session, address, command, service_name , ...)
		local f = cmd[command]
		if f == nil then
			skynet.ret(skynet.pack(nil))
			return
		end

		local ok, r = pcall(f, service_name, ...)
		if ok then
			skynet.ret(skynet.pack(r))
		else
			skynet.ret(skynet.pack(nil))
		end
	end)
	skynet.register(".service")
	if skynet.getenv "standalone" then
		skynet.register("SERVICE")
	end
end)
