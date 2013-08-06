local skynet = require "skynet"

skynet.start(function()
	print("Log server start")
	skynet.launch("socket",128)
	local service = skynet.newservice("service_mgr")
	skynet.monitor "simplemonitor"
	local lualog = skynet.newservice("lualog")
	local console = skynet.newservice("console")
	local log = skynet.newservice("globallog")
--	skynet.launch("snlua","testgroup_c 11 1")
	skynet.exit()
end)

