local skynet = require "skynet"
local log = require "log"
local config = require "config"

local redis_conf = skynet.getenv "redis"
local name = config (redis_conf)
local connection = {}

skynet.dispatch(function(msg, sz , session, from)
	local dbname = skynet.unpack(msg,sz)
	if connection[dbname] then
		skynet.ret(skynet.pack(connection[dbname]))
		return
	end
	if name[dbname] == nil then
		log.Error("Invalid db name : "..dbname)
		skynet.ret(skynet.pack(nil))
		return
	end

	local redis_cli = skynet.launch("snlua", "redis-cli", name[dbname])
	connection[dbname] = redis_cli
	skynet.ret(skynet.pack(redis_cli))
end)

skynet.register ".redis-manager"
