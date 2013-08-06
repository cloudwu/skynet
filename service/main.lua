local skynet = require "skynet"

skynet.start(function()
	print("Server start")
	skynet.launch("socket",128)
	local service = skynet.newservice("service_mgr")
	skynet.monitor "simplemonitor"
	local lualog = skynet.newservice("lualog")
	local console = skynet.newservice("console")
	local remoteroot = skynet.newservice("remote_root")
	local watchdog = skynet.newservice("watchdog","8888 4 0")
	local db = skynet.newservice("simpledb")
--	skynet.launch("snlua","testgroup")

	skynet.exit()
end)
