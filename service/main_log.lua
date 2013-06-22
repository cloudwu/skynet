local skynet = require "skynet"

skynet.start(function()
	print("Log server start")
	-- local connection = skynet.launch("connection","256")
	skynet.launch("socket",128)
	local lualog = skynet.launch("snlua","lualog")
	local console = skynet.launch("snlua","console")
	local log = skynet.launch("snlua","globallog")
--	skynet.launch("snlua","testgroup_c 11 1")
	skynet.exit()
end)

