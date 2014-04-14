local skynet = require "skynet"
local client

local CMD = {}

skynet.register_protocol {
	name = "client",
	id = skynet.PTYPE_CLIENT,
	pack = function(...) return ... end,
	unpack = skynet.tostring,
	dispatch = function (session, address, text)
		-- It's client, there is no session
		local result = skynet.call("SIMPLEDB", "text", text)
		skynet.ret(result)
	end
}

function CMD.start(gate , fd)
	client = skynet.launch("client", fd)
	skynet.call(gate, "lua", "forward", fd, client)
	skynet.send(client,"text","Welcome to skynet")
end

function CMD.exit()
	skynet.kill(client)
	client = nil
end

skynet.start(function()
	skynet.dispatch("lua", function(_,_, command, ...)
		local f = CMD[command]
		skynet.ret(skynet.pack(f(...)))
	end)
end)
