local skynet = require "skynet"

skynet.dispatch()

skynet.start(function()
	print("Log server start")
	local lualog = skynet.launch("snlua","lualog")
	local launcher = skynet.launch("snlua","launcher")
	local group_agent = skynet.launch("snlua", "group_agent")
	local console = skynet.launch("snlua","console")
	local log = skynet.launch("snlua","globallog")
--	skynet.launch("snlua","testgroup_c 11 1")
	skynet.exit()
end)

