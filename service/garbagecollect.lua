local skynet = require "skynet"
local ssm = require "skynet.ssm"

local function ssm_info()
	return ssm.info()
end

local function collect()
	local info = {}
	while true do
--		while ssm.collect(false, info) do
--			skynet.error(string.format("Collect %d strings from %s, sweep %d", info.n, info.key, info.sweep))
--		end
		ssm.collect(true)
		skynet.sleep(50)
	end
end

skynet.start(function()
	if ssm.disable then
		skynet.error "Short String Map (SSM) Disabled"
		skynet.exit()
	end
	skynet.info_func(ssm_info)
	skynet.fork(collect)
end)
