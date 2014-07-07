local skynet = require "skynet"

-- It's a simple service exit monitor, you can do something more when a service exit.

local service_map = {}

skynet.register_protocol {
	name = "client",
	id = skynet.PTYPE_CLIENT,	-- PTYPE_CLIENT = 3
	unpack = function() end,
	dispatch = function(_, address)
		local w = service_map[address]
		if w then
			for watcher in pairs(w) do
				skynet.redirect(watcher, address, "error", 0, "")
			end
			service_map[address] = false
		end
	end
}

local function monitor(session, watcher, command, service)
	assert(command, "WATCH")
	local w = service_map[service]
	if not w then
		if w == false then
			skynet.ret(skynet.pack(false))
			return
		end
		w = {}
		service_map[service] = w
	end
	w[watcher] = true
	skynet.ret(skynet.pack(true))
end

skynet.start(function()
	skynet.dispatch("lua", monitor)
end)
