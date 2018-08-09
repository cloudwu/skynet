local skynet = require "skynet"

local service = {}
local cache = {}
local provider

local function get_provider()
	provider = provider or skynet.uniqueservice "service_provider"
	return provider
end

local function check(func)
	local info = debug.getinfo(func, "u")
	assert(info.nups == 1)
	assert(debug.getupvalue(func,1) == "_ENV")
end

function service.new(name, mainfunc, ...)
	local p = get_provider()
	local addr, booting = skynet.call(p, "lua", "test", name)
	local address
	if addr then
		address = addr
	else
		if booting then
			address = skynet.call(p, "lua", "query", name)
		else
			check(mainfunc)
			local code = string.dump(mainfunc)
			address = skynet.call(p, "lua", "launch", name, code, ...)
		end
	end
	cache[name] = address
	return address
end

function service.query(name)
	if not cache[name] then
		cache[name] = skynet.call(get_provider(), "lua", "query", name)
	end
	return cache[name]
end

return service
