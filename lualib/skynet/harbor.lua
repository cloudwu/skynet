local skynet = require "skynet"

local harbor = {}

local HARBOR = skynet.getenv "harbor_address"

if HARBOR then
	HARBOR = tonumber("0x" .. string.sub(HARBOR , 2))
end

function harbor.globalname(name, handle)
	assert(HARBOR)
	handle = handle or skynet.self()
	skynet.redirect(HARBOR, handle, "system", 0, "R " .. name)
end

function harbor.init(h)
	assert(HARBOR == nil)
	HARBOR = h
	skynet.setenv("harbor_address", skynet.address(h))
end

function harbor.link(id)
	assert(HARBOR)
	skynet.call(HARBOR, "system", "M " .. tostring(id))
end

function harbor.connect(id)
	assert(HARBOR)
	skynet.call(HARBOR, "system", "C " .. tostring(id))
end

skynet.register_protocol {
	name = "system",
	id = skynet.PTYPE_SYSTEM,
	pack = function(...) return ... end,
	unpack = skynet.tostring,
}

return harbor
