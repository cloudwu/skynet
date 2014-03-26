local skynet = require "skynet"
local log = require "log"

skynet.start(function()
	log.Info("hello world")
	skynet.exit()
end)


