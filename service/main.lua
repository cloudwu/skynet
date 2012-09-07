local skynet = require "skynet"

skynet.start(function()
	print("Server start")
	local service = skynet.launch("snlua","service_mgr")
	local connection = skynet.launch("connection","256")
	local lualog = skynet.launch("snlua","lualog")
	local console = skynet.launch("snlua","console")
	local remoteroot = skynet.launch("snlua","remote_root")
	local watchdog = skynet.launch("snlua","watchdog","8888 4 0")
	local db = skynet.launch("snlua","simpledb")
--	skynet.launch("snlua","testgroup")

	skynet.exit()
end)
