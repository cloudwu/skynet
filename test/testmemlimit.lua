local skynet = require "skynet"

-- set sandbox memory limit to 1M, must set here (at start, out of skynet.start)
skynet.memlimit(1 * 1024 * 1024)

skynet.start(function()
	local a = {}
	local limit
	local ok, err = pcall(function()
		for i=1, 1000000 do
			limit = i
			table.insert(a, {})
		end
	end)
	skynet.error(limit, err)
	skynet.exit()
end)
