local skynet = require "skynet"
local client = ...

local dispatch_handler = skynet.default_dispatch(function (msg,sz)
	local message = skynet.tostring(msg,sz)
	local result = skynet.call("SIMPLEDB",message)
	skynet.ret(result)
end)

local session_id = 0
skynet.filter(function (session, address , msg, sz)
	if address == client then
		assert(session == 0)
		print("client message",skynet.tostring(msg,sz))
		-- It's client, there is no session
		session_id = session_id + 1
		session = - session_id
	else
		print("skynet message",msg,sz)
	end
	dispatch_handler(session,address, msg,sz)
end)

skynet.start(function()
	skynet.send(client,"Welcome to skynet")
end)
