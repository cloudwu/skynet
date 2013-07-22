local skynet = require "skynet"
local string = string

local services = {}

local command = {}

local function handle_to_address(handle)
	return tonumber("0x" .. string.sub(handle , 2))
end

function command.LIST()
	local list = {}
	for k,v in pairs(services) do
		list[skynet.address(k)] = v
	end
	skynet.ret(skynet.pack(list))
end

function command.RELOAD(handle)
	handle = handle_to_address(handle)
	local cmd = string.match(services[handle], "snlua (.+)")
	print(services[handle],cmd)
	if cmd then
		skynet.send(handle,"debug","RELOAD",cmd)
		skynet.ret(skynet.pack({cmd}))
	else
		skynet.ret(skynet.pack({"Support only snlua"}))
	end
end

function command.STAT()
	local list = {}
	for k,v in pairs(services) do
		local stat = skynet.call(k,"debug","STAT")
		list[skynet.address(k)] = stat
	end
	skynet.ret(skynet.pack(list))
end

function command.INFO(handle)
	handle = handle_to_address(handle)
	if services[handle] == nil then
		skynet.ret(skynet.pack(nil))
	else
		local result = skynet.call(handle,"debug","INFO")
		skynet.ret(skynet.pack(result))
	end
end

function command.KILL(handle)
	handle = handle_to_address(handle)
	skynet.kill(handle)
	skynet.ret( skynet.pack({ [handle] = tostring(services[handle]) }))
	services[handle] = nil
end

function command.MEM()
	local list = {}
	for k,v in pairs(services) do
		local kb, bytes = skynet.call(k,"debug","MEM")
		list[skynet.address(k)] = string.format("%d Kb (%s)",kb,v)
	end
	skynet.ret(skynet.pack(list))
end

function command.GC()
	for k,v in pairs(services) do
		skynet.send(k,"debug","GC")
	end
	command.MEM()
end

function command.REMOVE(handle)
	services[handle] = nil
end

local instance = {}

skynet.dispatch("text" , function(session, address , cmd)
	if cmd == "" then
		-- init notice
		local reply = instance[address]
		if reply then
			skynet.redirect(reply.address , 0, "response", reply.session, skynet.address(address))
			instance[address] = nil
		end
	elseif cmd == "ERROR" then
		-- see serivce-src/service_lua.c
		-- init failed
		local reply = instance[address]
		if reply then
			skynet.redirect(reply.address , 0, "response", reply.session, "")
			instance[address] = nil
		end
	else
		-- launch request
		local service, param = string.match(cmd,"([^ ]+) (.*)")
		local inst = skynet.launch(service, param)
		if inst then
			services[inst] = cmd
			instance[inst] = { session = session, address = address }
		else
			skynet.ret("")
		end
	end
end)

skynet.dispatch("lua", function(session, address, cmd , ...)
	cmd = string.upper(cmd)
	command[cmd](...)
end)

skynet.start(function() end)
