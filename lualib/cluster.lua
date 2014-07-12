local skynet = require "skynet"

local clusterd
local cluster = {}

function cluster.call(node, address, ...)
	-- skynet.pack(...) will free by cluster.c.packrequest
	return skynet.call(clusterd, "lua", "req", node, address, skynet.pack(...))
end

function cluster.open(port)
	if type(port) == "string" then
		skynet.call(clusterd, "lua", "listen", port)
	else
		skynet.call(clusterd, "lua", "listen", "0.0.0.0", port)
	end
end

function cluster.reload()
	skynet.call(clusterd, "lua", "reload")
end

skynet.init(function()
	clusterd = skynet.uniqueservice("clusterd")
end)

return cluster
