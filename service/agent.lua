local skynet = require "skynet"
local client = ...

skynet.dispatch(function(msg, sz , session, address)
	local message = skynet.tostring(msg,sz)
	print("command source",address)
	local result = skynet.call("SIMPLEDB",message)
	skynet.send(address, result)
end)

skynet.start(function()
	skynet.send(client,"Welcome to skynet")
end)
