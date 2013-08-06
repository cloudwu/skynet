local skynet = require "skynet"

-- It's a simple service exit monitor, you can do something more when a service exit.

skynet.register_protocol {
	name = "client",
	id = 3,
	unpack = function() end,
	dispatch = function(_, address)
		print(string.format("[:%x] exit", address))
	end
}

skynet.start(function() end)
