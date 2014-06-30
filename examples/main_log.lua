local skynet = require "skynet"

skynet.start(function()
	print("Log server start")
	skynet.monitor "simplemonitor"
	local log = skynet.newservice("globallog")
	skynet.exit()
end)

