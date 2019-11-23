local skynet = require "skynet"

skynet.start(function()
	skynet.newservice("multicast1")
	skynet.exit()
end)
