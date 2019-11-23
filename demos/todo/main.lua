local skynet = require "skynet"

skynet.start(function()
	skynet.error("hello, world")
	skynet.exit()
end)
