local skynet = require "skynet"

local command = {}

function command:open(parm)
	local fd,addr = string.match(parm,"(%d+) ([^%s]+)")
	fd = tonumber(fd)
	print("[watchdog] open",self,fd,addr)
	local agent = skynet.command("LAUNCH","snlua agent.lua ".. self)
	if agent then
		skynet.send(".gate","forward ".. self .. " " .. agent)
	end
end

function command:close()
	print("[watchdog] close",self)
end

function command:data(data)
	print("[watchdog] data",self,#data,data)
end

skynet.callback(function(from , message)
	local id, cmd , parm = string.match(message, "(%d+) (%w+) ?(.*)")
	id = tonumber(id)
	local f = command[cmd]
	if f then
		f(id,parm)
	else
		skynet.error(string.format("[watchdog] Unknown command : %s %d %s",cmd,id,parm))
	end
end)

skynet.command("REG","watchdog")