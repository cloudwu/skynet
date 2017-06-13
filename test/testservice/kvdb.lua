local skynet = require "skynet"
local service = require "skynet.service"

local kvdb = {}

-- service.address is the default address registered by itself.
function kvdb.get(key)
	return skynet.call(service.address, "lua", "get", key)
end

function kvdb.set(key, value)
	skynet.call(service.address, "lua", "set", key , value)
end

-- this function will be injected into an unique service, so don't refer any upvalues
local function service_mainfunc(...)
	local skynet = require "skynet"

	skynet.error(...)	-- (...) passed from service.new

	local db = {}

	local command = {}

	function command.get(key)
		return db[key]
	end

	function command.set(key, value)
		db[key] = value
	end

	-- skynet.start is compatible
	skynet.dispatch("lua", function(session, address, cmd, ...)
		skynet.ret(skynet.pack(command[cmd](...)))
	end)
end

skynet.init(function()
	service.new("kvdb", service_mainfunc, "Service Init")
end)

return kvdb
