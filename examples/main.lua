local skynet = require "skynet"

local max_client = 64

skynet.start(function()
	print("Server start")
	local service = skynet.newservice("service_mgr")
	skynet.monitor "simplemonitor"
	local lualog = skynet.newservice("lualog")
	local console = skynet.newservice("console")
--	skynet.newservice("debug_console",8000)
	local watchdog = skynet.newservice("watchdog","8888", max_client, 0)
	local db = skynet.newservice("simpledb")
--	skynet.launch("snlua","testgroup")

	skynet.exit()
end)
