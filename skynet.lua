local c = require "skynet.c"
local tostring = tostring
local tonumber = tonumber
local coroutine = coroutine
local assert = assert

local skynet = {}
local session_id_coroutine = {}
local session_coroutine_id = {}
local session_coroutine_address = {}

local function suspend(co, result, command, param)
	assert(result, command)
	if command == "CALL" or command == "SLEEP" then
		session_id_coroutine[param] = co
	elseif command == "RETURN" then
		local co_session = session_coroutine_id[co]
		local co_address = session_coroutine_address[co]
		c.send(co_address, co_session, param)
	else
		assert(command == nil, command)
		session_coroutine_id[co] = nil
		session_coroutine_address[co] = nil
	end
end

function skynet.timeout(ti, func, ...)
	local session = c.command("TIMEOUT",tostring(ti))
	assert(session)
	session = tonumber(session)
	local co = coroutine.create(func)
	assert(session_id_coroutine[session] == nil)
	session_id_coroutine[session] = co
	suspend(co, coroutine.resume(co, ...))
end

function skynet.yield()
	local session = c.command("TIMEOUT","0")
	coroutine.yield("SLEEP", tonumber(session))
end

function skynet.sleep(ti)
	local session = c.command("TIMEOUT",tostring(ti))
	assert(session)
	coroutine.yield("SLEEP", tonumber(session))
end

function skynet.register(name)
	return c.command("REG", name)
end

function skynet.self()
	return c.command("REG")
end

function skynet.launch(...)
	return c.command("LAUNCH", table.concat({...}," "))
end

function skynet.now()
	return tonumber(c.command("NOW"))
end

function skynet.exit()
	c.command("EXIT")
end

skynet.send = c.send

function skynet.call(addr, message)
	local session = c.send(addr, -1, message)
	return coroutine.yield("CALL", session)
end

function skynet.ret(message)
	coroutine.yield("RETURN", message)
end

function skynet.dispatch(f)
	c.callback(function(session, address , message)
		local co = session_id_coroutine[session]
		if co == nil then
			co = coroutine.create(f)
			session_coroutine_id[co] = session
			session_coroutine_address[co] = address
			suspend(co, coroutine.resume(co, message, address, session))
		else
			session_id_coroutine[session] = nil
			suspend(co, coroutine.resume(co, message))
		end
	end)
end

function skynet.start(f)
	c.command("TIMEOUT",0,"0")
	local co = coroutine.create(f)
	session_id_coroutine[0] = co
end

return skynet
