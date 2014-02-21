local skynet = require "skynet"
local client = ...

skynet.register_protocol {
	name = "client",
	id = skynet.PTYPE_CLIENT,
	pack = function(...) return ... end,
	unpack = skynet.tostring,
	dispatch = function (session, address, text)
		-- It's client, there is no session
		skynet.send("LOG", "text", "client message :" .. text)
		local result = skynet.call("SIMPLEDB", "text", text)
		skynet.ret(result)
	end
}

skynet.start(function()
	skynet.send(client,"text","Welcome to skynet")
end)
