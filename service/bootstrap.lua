local skynet = require "skynet"

skynet.start(function()
	local launcher = assert(skynet.launch("snlua launcher"))
	skynet.name(".launcher", launcher)

	if skynet.getenv "standalone" then
		local smgr = assert(skynet.newservice "service_mgr")
		skynet.name("SERVICE", smgr)
	end
	assert(skynet.newservice(skynet.getenv "start" or "main"))
	skynet.exit()
end)
