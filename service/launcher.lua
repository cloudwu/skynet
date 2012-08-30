local skynet = require "skynet"
local string = string

local instance = {}

local function _extract( service, ...)
	return service , table.concat({...}, " ")
end

skynet.dispatch(function(msg, sz , session, address)
	if session == 0 then
		-- init notice
		local reply = instance[address]
		if reply then
			skynet.send(reply[2] , reply[1], skynet.pack(address))
			instance[address] = nil
		end
	else
		-- launch request
		local service , param = _extract(skynet.unpack(msg,sz))
		local inst = skynet.launch(service, param)
		if inst then
			instance[inst] = { session, address }
		else
			skynet.send(address, session, skynet.pack(nil))
		end
	end
end)

skynet.register(".launcher")