local skynet = require "skynet"

local clusterd
local cluster = {}

function cluster.call(node, address, ...)
	-- skynet.pack(...) will free by cluster.core.packrequest
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

function cluster.proxy(node, name)
	return skynet.call(clusterd, "lua", "proxy", node, name)
end

local namecache = {}

function cluster.ncall(name, ...)
	local s = namecache[name]
	if not s then
		local node , lname = name:match "(.-)(%..+)"
		assert(node and lname)
		s = cluster.proxy(node, lname)
		namecache[name] = assert(s)
	end
	return skynet.call(s, "lua", ...)
end

skynet.init(function()
	clusterd = skynet.uniqueservice("clusterd")
end)

return cluster
