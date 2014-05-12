local skynet = require "skynet"
local string = string

local services = {}
local command = {}
local instance = {} -- for confirm (function command.LAUNCH / command.ERROR / command.LAUNCHOK)

local function handle_to_address(handle)
	return tonumber("0x" .. string.sub(handle , 2))
end

local NORET = {}

function command.LIST()
	local list = {}
	for k,v in pairs(services) do
		list[skynet.address(k)] = v
	end
	return list
end

function command.STAT()
	local list = {}
	for k,v in pairs(services) do
		local stat = skynet.call(k,"debug","STAT")
		list[skynet.address(k)] = stat
	end
	return list
end

function command.INFO(_, _, handle)
	handle = handle_to_address(handle)
	if services[handle] == nil then
		return
	else
		local result = skynet.call(handle,"debug","INFO")
		return result
	end
end

function command.KILL(_, _, handle)
	handle = handle_to_address(handle)
	skynet.kill(handle)
	local ret = { [skynet.address(handle)] = tostring(services[handle]) }
	services[handle] = nil
	return ret
end

function command.MEM()
	local list = {}
	for k,v in pairs(services) do
		local kb, bytes = skynet.call(k,"debug","MEM")
		list[skynet.address(k)] = string.format("%d Kb (%s)",kb,v)
	end
	return list
end

function command.GC()
	for k,v in pairs(services) do
		skynet.send(k,"debug","GC")
	end
	return command.MEM()
end

function command.REMOVE(_,_, handle)
	services[handle] = nil
	-- don't return (skynet.ret) because the handle may exit
	return NORET
end

function command.LAUNCH(address, session, service, ...)
	local param = table.concat({...}, " ")
	local inst = skynet.launch(service, param)
	if inst then
		services[inst] = service .. " " .. param
		instance[inst] = { session = session, address = address }
	else
		skynet.ret("")	-- launch failed
	end
	return NORET
end

function command.ERROR(address)
	-- see serivce-src/service_lua.c
	-- init failed
	local reply = instance[address]
	if reply then
		skynet.redirect(reply.address , 0, "response", reply.session, "")
		instance[address] = nil
	end

	return NORET
end

function command.LAUNCHOK(address)
	-- init notice
	local reply = instance[address]
	if reply then
		skynet.redirect(reply.address , 0, "response", reply.session, skynet.address(address))
		instance[address] = nil
	end

	return NORET
end

-- for historical reasons, launcher support text command (for C service)

skynet.register_protocol {
	name = "text",
	id = skynet.PTYPE_TEXT,
	unpack = skynet.tostring,
	dispatch = function(session, address , cmd)
		if cmd == "" then
			command.LAUNCHOK(address)
		elseif cmd == "ERROR" then
			command.ERROR(address)
		else
			-- launch request
			local service, param = string.match(cmd,"([^ ]+) (.*)")
			command.LAUNCH(address, session, service, param)
		end
	end,
}

skynet.dispatch("lua", function(session, address, cmd , ...)
	cmd = string.upper(cmd)
	local f = command[cmd]
	if f then
		local ret = f(address, session, ...)
		if ret ~= NORET then
			skynet.ret(skynet.pack(ret))
		end
	else
		skynet.ret(skynet.pack {"Unknown command"} )
	end
end)

skynet.start(function() end)
