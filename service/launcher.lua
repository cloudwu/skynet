local skynet = require "skynet"
local string = string

local instance = {}

skynet.dispatch(function(msg, sz , session, address)
	local message = skynet.tostring(msg,sz)
	if session == 0 then
		-- init notice
		local reply = instance[address]
		if reply then
			skynet.send(reply[2] , reply[1], skynet.address(address))
			instance[address] = nil
		end
	else
		-- launch request
		local service, param = string.match(message, "([^ ]+) (.*)")
		local inst = skynet.launch(service, param)
		if inst then
			instance[inst] = { session, address }
		else
			skynet.send(address, session)
		end
	end
end)

skynet.register(".launcher")