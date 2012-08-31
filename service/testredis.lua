local skynet = require "skynet"
local redis = require "redis"

skynet.start(function()
	local db = redis.connect "main"
	print(db:exists "A")
	print(db:get "A")
	print(db:set("A","hello world"))
	skynet.exit()
end)

