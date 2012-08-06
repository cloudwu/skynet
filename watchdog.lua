local skynet = require "skynet"

local command = {}
local agent_all = {}

function command:open(parm)
	local fd,addr = string.match(parm,"(%d+) ([^%s]+)")
	fd = tonumber(fd)
	skynet.send("LOG", 0, string.format("%d %d %s",self,fd,addr))
	local client = skynet.launch("client",fd)
	print("client",client)
	local agent = skynet.launch("snlua","agent.lua",client)
	if agent then
		agent_all[self] = agent
		skynet.send("gate",0, "forward ".. self .. " " .. agent)
	end
end

function command:close()
	skynet.send("LOG",0,string.format("close %d",self))
	skynet.send(agent_all[self],1,"CLOSE")
	agent_all[self] = nil
end

function command:data(data)
	local agent = agent_all[self]
	if agent then
		skynet.send(agent,0,data)
	else
		skynet.send("LOG",0,string.format("data %d size=%d",self,#data))
	end
end

skynet.dispatch(function(message)
	local id, cmd , parm = string.match(message, "(%d+) (%w+) ?(.*)")
	id = tonumber(id)
	local f = command[cmd]
	if f then
		f(id,parm)
	else
		skynet.error(string.format("[watchdog] Unknown command : %s",message))
	end
end)

skynet.register ".watchdog"