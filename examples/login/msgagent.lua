local skynet = require "skynet"

skynet.register_protocol {
	name = "client",
	id = skynet.PTYPE_CLIENT,
	unpack = skynet.tostring,
}

local gate

local CMD = {}

function CMD.init(source , uid, server)
	gate = source
	skynet.error(string.format("%s is coming", uid))
end

skynet.start(function()
	skynet.dispatch("lua", function(_, source, command, ...)
		local f = assert(CMD[command])
		skynet.ret(skynet.pack(f(source, ...)))
	end)

	skynet.dispatch("client", function(_,_, msg)
		skynet.ret(msg)
	end)
end)
