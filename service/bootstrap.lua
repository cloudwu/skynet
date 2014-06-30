local skynet = require "skynet"
local harbor = require "skynet.harbor"

skynet.start(function()
	local standalone = skynet.getenv "standalone"

	local launcher = assert(skynet.launch("snlua","launcher"))
	skynet.name(".launcher", launcher)

	local harbor_id = tonumber(skynet.getenv "harbor")
	if harbor_id == 0 then
		assert(standalone ==  nil)
		standalone = true
		skynet.setenv("standalone", "true")

		local slave = skynet.newservice "cdummy"
		if slave == nil then
			skynet.abort()
		end
		skynet.name(".slave", slave)

	else
		if standalone then
			if not skynet.newservice "cmaster" then
				skynet.abort()
			end
		end

		local slave = skynet.newservice "cslave"
		if slave == nil then
			skynet.abort()
		end
		skynet.name(".slave", slave)
	end

	if standalone then
		local datacenter = assert(skynet.newservice "datacenterd")
		skynet.name("DATACENTER", datacenter)
	end
	assert(skynet.newservice "service_mgr")
	assert(skynet.newservice(skynet.getenv "start" or "main"))
	skynet.exit()
end)
