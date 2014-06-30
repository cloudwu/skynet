local skynet = require "skynet"

local globalname = {}
local harbor = {}

skynet.register_protocol {
	name = "harbor",
	id = skynet.PTYPE_HARBOR,
	pack = function(...) return ... end,
	unpack = skynet.tostring,
}

skynet.register_protocol {
	name = "text",
	id = skynet.PTYPE_TEXT,
	pack = function(...) return ... end,
	unpack = skynet.tostring,
}

function harbor.REGISTER(name, handle)
	assert(globalname[name] == nil)
	globalname[name] = handle
	skynet.redirect(harbor_service, handle, "harbor", 0, "N " .. name)
end

function harbor.LINK(id)
	skynet.ret()
end

function harbor.CONNECT(id)
	skynet.error("Can't connect to other harbor in single node mode")
end

skynet.start(function()
	local harbor_id = tonumber(skynet.getenv "harbor")
	assert(harbor_id == 0)

	skynet.dispatch("lua", function (session,source,command,...)
		local f = assert(harbor[command])
		f(...)
	end)
	skynet.dispatch("text", function(session,source,command)
		-- ignore all the command
	end)

	harbor_service = assert(skynet.launch("harbor", harbor_id, skynet.self()))
end)
