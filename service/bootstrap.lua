local skynet = require "skynet"

skynet.start(function()
	assert(skynet.launch("logger", skynet.getenv "logger"))

	local standalone = skynet.getenv "standalone"
	local master_addr = skynet.getenv "master"

	if standalone then
		assert(skynet.launch("master", master_addr))
	end

	local local_addr = skynet.getenv "address"
	local harbor_id = skynet.getenv "harbor"

	assert(skynet.launch("harbor",master_addr, local_addr, harbor_id))

	local launcher = assert(skynet.launch("snlua","launcher"))
	skynet.name(".launcher", launcher)

	if standalone then
		local datacenter = assert(skynet.newservice "datacenterd")
		skynet.name("DATACENTER", datacenter)
	end
	assert(skynet.newservice "service_mgr")
	assert(skynet.newservice(skynet.getenv "start" or "main"))
	skynet.exit()
end)
