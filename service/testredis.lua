local skynet = require "skynet"
local redis = require "redis"

skynet.dispatch()

skynet.start(function()
	print(redis.cmd.EXISTS("A"))
	print(redis.cmd.GET("A"))
	print(redis.cmd.SET("A","hello world"))
	skynet.exit()
end)

