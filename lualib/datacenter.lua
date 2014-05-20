local skynet = require "skynet"

local datacenter = {}

function datacenter.get(...)
	return skynet.call("DATACENTER", "lua", "QUERY", ...)
end

function datacenter.set(...)
	return skynet.call("DATACENTER", "lua", "UPDATE", ...)
end

return datacenter

