local skynet = require "skynet"
require "skynet.manager"

skynet.register_protocol {
	name = "text",
	id = skynet.PTYPE_TEXT,
	unpack = skynet.tostring,
	dispatch = function(_, address, msg)
		print(string.format("%x(%.2f): %s", address, skynet.time(), msg))
	end
}

skynet.start(function()
	skynet.register ".logger"
end)