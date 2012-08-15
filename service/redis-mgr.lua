local skynet = require "skynet"
local log = require "log"

local name = {
	main = "127.0.0.1:6379",
}

local connection = {}

skynet.dispatch(function(msg, sz , session, from)
	local dbname = skynet.tostring(msg,sz)
	if connection[dbname] then
		skynet.ret(connection[dbname])
		return
	end
	if name[dbname] == nil then
		log.Error("Invalid db name : "..dbname)
		skynet.ret("")
		return
	end

	local redis_cli = skynet.launch("snlua", "redis-cli", name[dbname])
	connection[dbname] = redis_cli
	skynet.ret(redis_cli)
end)

skynet.register ".redis-manager"
