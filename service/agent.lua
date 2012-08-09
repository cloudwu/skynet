local skynet = require "skynet"
local client = ...

skynet.dispatch(function(msg, sz , session, address)
	local message = skynet.tostring(msg,sz)
	if session == 0 then
		print("client command",message)
		local result = skynet.call("SIMPLEDB",message)
		skynet.send(client, result)
	else
		print("server command",message)
		if msg == "CLOSE" then
			skynet.kill(client)
			skynet.exit()
		end
	end
end)
