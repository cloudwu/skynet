local skynet = require "skynet"
local service = require "skynet.service"

local kvdb = {}

function kvdb.get(db,key)
	return skynet.call(service.query(db), "lua", "get", key)
end

function kvdb.set(db,key, value)
	skynet.call(service.query(db), "lua", "set", key , value)
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

function kvdb.new(db)
	return service.new(db, service_mainfunc, "Service Init")
end

return kvdb
