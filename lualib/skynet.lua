local c = require "skynet.c"
local tostring = tostring
local tonumber = tonumber
local coroutine = coroutine
local assert = assert
local pairs = pairs

local skynet = {}
local session_id_coroutine = {}
local session_coroutine_id = {}
local session_coroutine_address = {}

local wakeup_session = {}
local sleep_session = {}

-- suspend is function
local suspend

local function dispatch_wakeup()
	local co = next(wakeup_session)
	if co then
		wakeup_session[co] = nil
		local session = sleep_session[co]
		if session then
			session_id_coroutine[session] = "BREAK"
			return suspend(co, coroutine.resume(co, true))
		end
	end
end

-- suspend is local function
function suspend(co, result, command, param, size)
	if not result then
		error(debug.traceback(co,command))
	end
	if command == "CALL" then
		session_id_coroutine[param] = co
	elseif command == "SLEEP" then
		session_id_coroutine[param] = co
		sleep_session[co] = param
	elseif command == "RETURN" then
		local co_session = session_coroutine_id[co]
		local co_address = session_coroutine_address[co]
		c.send(co_address, co_session, param, size)
		return suspend(co, coroutine.resume(co))
	elseif command == nil then
		-- coroutine exit
		session_coroutine_id[co] = nil
		session_coroutine_address[co] = nil
	else
		error("Unknown command : " .. command .. "\n" .. debug.traceback(co))
	end
	dispatch_wakeup()
end

function skynet.timeout(ti, func)
	local session = c.command("TIMEOUT",tostring(ti))
	assert(session)
	session = tonumber(session)
	local co = coroutine.create(func)
	assert(session_id_coroutine[session] == nil)
	session_id_coroutine[session] = co
end

function skynet.fork(func,...)
	local args = { ... }
	skynet.timeout("0", function()
		func(unpack(args))
	end)
end

function skynet.sleep(ti)
	local session = c.command("TIMEOUT",tostring(ti))
	assert(session)
	local ret = coroutine.yield("SLEEP", tonumber(session))
	sleep_session[coroutine.running()] = nil
	return ret
end

function skynet.yield()
	local session = c.command("TIMEOUT","0")
	assert(session)
	coroutine.yield("SLEEP", tonumber(session))
	sleep_session[coroutine.running()] = nil
end

function skynet.register(name)
	return c.command("REG", name)
end

function skynet.name(name, handle)
	c.command("NAME", name .. " " .. handle)
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

function skynet.starttime()
	return tonumber(c.command("STARTTIME"))
end

function skynet.exit()
	c.command("EXIT")
end

function skynet.kill(name)
	c.command("KILL",name)
end

function skynet.getenv(key)
	return c.command("GETENV",key)
end

function skynet.setenv(key, value)
	c.command("SETENV",key .. " " ..value)
end

skynet.send = assert(c.send)
skynet.genid = assert(c.genid)
skynet.redirect = assert(c.redirect)
skynet.pack = assert(c.pack)
skynet.tostring = assert(c.tostring)
skynet.unpack = assert(c.unpack)

function skynet.call(addr, deseri , ...)
	if deseri == nil then
		local session = c.send(addr, -1, ...)
		return coroutine.yield("CALL", session)
	end
	local t = type(deseri)
	if t == "function" then
		local session = c.send(addr, -1, ...)
		return deseri(coroutine.yield("CALL", session))
	else
		assert(t=="string")
		local session = c.send(addr, -1, deseri)
		return c.tostring(coroutine.yield("CALL", session))
	end
end

function skynet.ret(...)
	coroutine.yield("RETURN", ...)
end

local function default_dispatch(f, unknown)
	unknown = unknown or function (session, msg, sz)
		local self = skynet.self()
		print(self, session,msg,sz)
		error(string.format("%s Unknown session %d", self, session))
	end
	return function(session, address , msg, sz)
		if session == nil then
			return
		end
		if session <= 0 then
			session = - session
			co = coroutine.create(f)
			session_coroutine_id[co] = session
			session_coroutine_address[co] = address
			suspend(co, coroutine.resume(co, msg, sz, session, address))
		else
			local co = session_id_coroutine[session]
			if co == "BREAK" then
				session_id_coroutine[session] = nil
			elseif co == nil then
				unknown(session,msg,sz)
			else
				session_id_coroutine[session] = nil
				suspend(co, coroutine.resume(co, msg, sz))
			end
		end
	end
end

function skynet.wakeup(co)
	if sleep_session[co] and wakeup_session[co] == nil then
		wakeup_session[co] = true
		return true
	end
end

function skynet.dispatch(f,unknown)
	c.callback(default_dispatch(f,unknown))
end

function skynet.filter(filter, f, unknown)
	local func = default_dispatch(f, unknown)
	c.callback(function (...)
		func(filter(...))
	end)
end

function skynet.start(f)
	local session = c.command("TIMEOUT","0")
	local co = coroutine.create(
		function(...)
			f(...)
			skynet.send(".launcher",0)
		end
	)
	session_id_coroutine[tonumber(session)] = co
end

function skynet.newservice(name, ...)
	local args = { "snlua" , name , ... }
	local handle = skynet.call(".launcher", table.concat(args," "))
	return handle
end

------ remote object --------

do
	local remote_query, remote_alloc, remote_bind = c.remote_init(skynet.self())
	local weak_meta = { __mode = "kv" }
	local meta = getmetatable(c.unpack(c.pack({ __remote = 0 })))
	local remote_call_func = setmetatable({}, weak_meta)
	setmetatable(meta, weak_meta)

	local _send = assert(c.send)
	local _yield = coroutine.yield
	local _pack = assert(c.pack)
	local _unpack = assert(c.unpack)
	local _local = skynet.self()

	function meta__index(t, method)
		local f = remote_call_func[method]
		if f == nil then
			f = function(...)
				local addr = remote_query(t.__remote)
				local session = _send(addr, -1, _pack(t,method,...))
				local msg, sz = _yield("CALL", session)
				return select(2,assert(_unpack(msg,sz)))
			end
			remote_call_func[method] = f
		end
		rawset(t,method,f)
		return f
	end

	-- prevent gc
	meta.__index = meta__index

	meta.__newindex = error

	function skynet.remote_create(t, handle)
		t = t or {}
		if handle then
			remote_bind(handle)
		else
			handle = remote_alloc()
		end
		rawset(t, "__remote" , handle)
		rawset(meta, handle, t)
		return t
	end

	function skynet.remote_bind(handle)
		return setmetatable( { __remote = handle } , meta)
	end

	local function remote_call(obj, method, ...)
		if type(obj) ~= "table" or type(method) ~= "string" then
			return _yield("RETURN", _pack(false, "Invalid call"))
		end
		local f = obj[method]
		if type(f) ~= "function" then
			return _yield("RETURN", _pack(false, "Object has not method " .. method))
		end
		return _yield("RETURN", _pack(pcall(f,...)))
	end

	function skynet.remote_service(unknown)
		local function f(msg,sz)
			return remote_call(_unpack(msg,sz))
		end
		c.callback(default_dispatch(f,unknown))
	end

	function skynet.remote_root()
		return skynet.remote_bind(0)
	end
end

return skynet
