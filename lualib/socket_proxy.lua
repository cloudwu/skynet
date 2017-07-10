local skynet = require "skynet"

local proxyd

skynet.init(function()
	proxyd = skynet.uniqueservice "socket_proxyd"
end)

local proxy = {}
local map = {}

skynet.register_protocol {
	name = "text",
	id = skynet.PTYPE_TEXT,
	pack = function(text) return text end,
	unpack = function(buf, sz) return skynet.tostring(buf,sz) end,
}

skynet.register_protocol {
	name = "client",
	id = skynet.PTYPE_CLIENT,
	pack = function(buf, sz) return buf, sz end,
}

local function get_addr(fd)
	return assert(map[fd], "subscribe first")
end

function proxy.subscribe(fd)
	local addr = map[fd]
	if not addr then
		addr = skynet.call(proxyd, "lua", fd)
		map[fd] = addr
	end
end

function proxy.read(fd)
	return skynet.rawcall(get_addr(fd), "text", "R")
end

function proxy.write(fd, msg, sz)
	skynet.send(get_addr(fd), "client", msg, sz)
end

function proxy.close(fd)
	skynet.send(get_addr(fd), "text", "K")
end

function proxy.info(fd)
	return skynet.call(get_addr(fd), "text", "I")
end

return proxy



