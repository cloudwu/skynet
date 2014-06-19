local skynet = require "skynet"
local harbor = require "skynet.harbor"

skynet.start(function()
	local standalone = skynet.getenv "standalone"
	local harbor_id = tonumber(skynet.getenv "harbor")
	if harbor_id == 0 then
		assert(standalone ==  nil)
		standalone = true
		skynet.setenv("standalone", "true")
		local dummy = assert(skynet.launch("dummy"))
		harbor.init(dummy)
	else
		local master_addr = skynet.getenv "master"

		if standalone then
			assert(skynet.launch("master", standalone))
		end

		local local_addr = skynet.getenv "address"

		local h = assert(skynet.launch("harbor",master_addr, local_addr, harbor_id))
		harbor.init(h)
	end

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
