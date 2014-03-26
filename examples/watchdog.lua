local skynet = require "skynet"

local port, max_agent, buffer = ...
local command = {}
local agent_all = {}
local gate

function command:open(parm)
	local fd,addr = string.match(parm,"(%d+) ([^%s]+)")
	fd = tonumber(fd)
	print("agent open",self,string.format("%d %d %s",self,fd,addr))
	local client = skynet.launch("client",fd)
	local agent = skynet.launch("snlua","agent",skynet.address(client))
	if agent then
		agent_all[self] = { agent , client }
		skynet.send(gate, "text", "forward" , self, skynet.address(agent) , skynet.address(client))
	end
end

function command:close()
	print("agent close",self,string.format("close %d",self))
	local agent = agent_all[self]
	agent_all[self] = nil
	skynet.kill(agent[1])
	skynet.kill(agent[2])
end

function command:data(data, session)
	local agent = agent_all[self]
	if agent then
		-- PTYPE_CLIENT = 3 , read skynet.h
		skynet.redirect(agent[1], agent[2], "client", 0, data)
	else
		skynet.error(string.format("agent data drop %d size=%d",self,#data))
	end
end

skynet.register_protocol {
	name = "client",
	id = skynet.PTYPE_CLIENT,
}

skynet.start(function()
	skynet.dispatch("text", function(session, from, message)
		local id, cmd , parm = string.match(message, "(%d+) (%w+) ?(.*)")
		id = tonumber(id)
		local f = command[cmd]
		if f then
			f(id,parm,session)
		else
			error(string.format("[watchdog] Unknown command : %s",message))
		end
	end)
	-- 0 for default client tag
	gate = skynet.launch("gate" , "S" , skynet.address(skynet.self()), port, 0, max_agent, buffer)
	skynet.send(gate,"text", "start")
	skynet.register(".watchdog")
end)
