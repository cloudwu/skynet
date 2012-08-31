local skynet = require "skynet"

skynet.start(function()
	print("Server start")
	local launcher = skynet.launch("snlua","launcher")
	local lualog = skynet.launch("snlua","lualog")
	local group_mgr = skynet.launch("snlua", "group_mgr")
	local group_agent = skynet.launch("snlua", "group_agent")
	local remoteroot = skynet.launch("snlua","remote_root")
	local console = skynet.launch("snlua","console")
	local watchdog = skynet.launch("snlua","watchdog","8888 4 0")
	local db = skynet.launch("snlua","simpledb")
	local connection = skynet.launch("connection","256")
	local redis = skynet.launch("snlua","redis-mgr")
--	skynet.launch("snlua","testgroup")

	skynet.exit()
end)
