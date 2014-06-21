local skynet = require "skynet"

local harbor = {}

function harbor.globalname(name, handle)
	handle = handle or skynet.self()
	skynet.send(".slave", "lua", "REGISTER", name, handle)
end

function harbor.link(id)
	skynet.call(".slave", "lua", "LINK", id)
end

function harbor.connect(id)
	skynet.call(".slave", "lua", "CONNECT", id)
end

return harbor
