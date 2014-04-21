local skynet = require "skynet"

skynet.start(function()
	print("Log server start")
	local service = skynet.newservice("service_mgr")
	skynet.monitor "simplemonitor"
	local log = skynet.newservice("globallog")
	skynet.exit()
end)

