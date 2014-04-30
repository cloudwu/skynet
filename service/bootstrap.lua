local skynet = require "skynet"

skynet.start(function()
	local launcher = assert(skynet.launch("snlua launcher"))
	skynet.name(".launcher", launcher)

	if skynet.getenv "standalone" then
		local datacenter = assert(skynet.newservice "datacenterd")
		skynet.name("DATACENTER", datacenter)
	end
	assert(skynet.newservice "service_mgr")
	assert(skynet.newservice(skynet.getenv "start" or "main"))
	skynet.exit()
end)
