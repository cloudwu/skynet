local skynet = require "skynet"

skynet.start(function()
	local launcher = assert(skynet.launch("snlua launcher"))
	skynet.name(".launcher", launcher)

	if skynet.getenv "standalone" then
		local datacenter = assert(skynet.newservice "datacenter")
		skynet.name("DATACENTER", datacenter)
		local smgr = assert(skynet.newservice "service_mgr")
	end
	assert(skynet.newservice(skynet.getenv "start" or "main"))
	skynet.exit()
end)
