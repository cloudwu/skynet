local skynet = require "skynet"
local string = string

local instance = {}

skynet.register(".launcher")

skynet.start(function()
	skynet.dispatch("lua" , function(session, address , service, ...)
		if service == nil then
			-- init notice
			local reply = instance[address]
			if reply then
				skynet.redirect(reply.address , 0, "response", reply.session, skynet.pack(address))
				instance[address] = nil
			end
		else
			-- launch request
			local param =  table.concat({...}, " ")
			local inst = skynet.launch(service, param)
			if inst then
				instance[inst] = { session = session, address = address }
			else
				skynet.ret(skynet.pack(nil))
			end
		end
	end)
end)
