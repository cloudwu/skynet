local skynet = require "skynet"
local client = ...

skynet.dispatch(function(msg,session)
	if session == 0 then
		print("client command",msg)
		local result = skynet.call("SIMPLEDB",msg)
		skynet.send(client,0,result)
	else
		print("server command",msg)
		if msg == "CLOSE" then
			skynet.kill(client)
			skynet.exit()
		end
	end
end)
