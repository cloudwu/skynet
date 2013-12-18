local c = require "skynet.c"
local mc = require "mcast.c"
local tostring = tostring
local tonumber = tonumber
local coroutine = coroutine
local assert = assert
local pairs = pairs
local pcall = pcall

local proto = {}
local skynet = {
	-- read skynet.h
	PTYPE_TEXT = 0,
	PTYPE_RESPONSE = 1,
	PTYPE_MULTICAST = 2,
	PTYPE_CLIENT = 3,
	PTYPE_SYSTEM = 4,
	PTYPE_HARBOR = 5,
	PTYPE_SOCKET = 6,
	PTYPE_ERROR = 7,
	PTYPE_QUEUE = 8,
	PTYPE_DEBUG = 9,
	PTYPE_LUA = 10
}

-- code cache
skynet.cache = require "skynet.codecache"

function skynet.register_protocol(class)
	local name = class.name
	local id = class.id
	assert(proto[name] == nil)
	assert(type(name) == "string" and type(id) == "number" and id >=0 and id <=255)
	proto[name] = class
	proto[id] = class
end

local session_id_coroutine = {}
local session_coroutine_id = {}
local session_coroutine_address = {}

local wakeup_session = {}
local sleep_session = {}

local watching_service = {}
local watching_session = {}
local error_queue = {}

-- suspend is function
local suspend

local trace_handle
local trace_func = function() end

local function string_to_handle(str)
	return tonumber("0x" .. string.sub(str , 2))
end

----- monitor exit

local function dispatch_error_queue()
	local session = table.remove(error_queue,1)
	if session then
		local co = session_id_coroutine[session]
		session_id_coroutine[session] = nil
		c.trace_switch(trace_handle, session)
		return suspend(co, coroutine.resume(co, false))
	end
end

local function _error_dispatch(error_session, monitor, service)
	if service then
		-- service is down
		--  Don't remove from watching_service , because user may call dead service
		watching_service[service] = false
		for session, srv in pairs(watching_session) do
			if srv == service then
				table.insert(error_queue, session)
			end
		end
	else
		-- capture an error for error_session
		if watching_session[error_session] then
			table.insert(error_queue, error_session)
		end
	end
end

local watch_monitor

function skynet.watch(service)
	assert(type(service) == "number")
	if watch_monitor == nil then
		watch_monitor = string_to_handle(c.command("MONITOR"))
		assert(watch_monitor, "Need a monitor")
	end
	if watching_service[service] == nil then
		watching_service[service] = true
		-- read lualib/simplemonitor.lua
		assert(skynet.call(watch_monitor, "lua", "WATCH", service), "watch a dead service")
	end
end

-- coroutine reuse

local coroutine_pool = {}
local coroutine_yield = coroutine.yield

local function co_create(f)
	local co = table.remove(coroutine_pool)
	if co == nil then
		co = coroutine.create(function(...)
			f(...)
			while true do
				f = nil
				coroutine_pool[#coroutine_pool] = co
				f = coroutine_yield "EXIT"
				f(coroutine_yield())
			end
		end)
	else
		coroutine.resume(co, f)
	end
	return co
end

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

local function trace_count()
	local info = c.trace_yield(trace_handle)
	if info then
		local ti = c.trace_delete(trace_handle, info)
		trace_func(info, ti)
	end
end

-- suspend is local function
function suspend(co, result, command, param, size)
	if not result then
		trace_count()
		local session = session_coroutine_id[co]
		local addr = session_coroutine_address[co]
		if session and session ~= 0  then
			c.send(addr, skynet.PTYPE_ERROR, session, "")
		end
		session_coroutine_id[co] = nil
		session_coroutine_address[co] = nil
		error(debug.traceback(co,command))
	end
	if command == "CALL" then
		c.trace_register(trace_handle, param)
		session_id_coroutine[param] = co
	elseif command == "SLEEP" then
		c.trace_register(trace_handle, param)
		session_id_coroutine[param] = co
		sleep_session[co] = param
	elseif command == "RETURN" then
		local co_session = session_coroutine_id[co]
		local co_address = session_coroutine_address[co]
		if param == nil then
			trace_count()
			error(debug.traceback(co))
		end
		-- c.send maybe throw a error, so call trace_count first.
		-- The coroutine execute time after skynet.ret() will not be trace.
		trace_count()
		c.send(co_address, skynet.PTYPE_RESPONSE, co_session, param, size)
		return suspend(co, coroutine.resume(co))
	elseif command == "EXIT" then
		-- coroutine exit
		session_coroutine_id[co] = nil
		session_coroutine_address[co] = nil
	else
		trace_count()
		error("Unknown command : " .. command .. "\n" .. debug.traceback(co))
	end
	trace_count()
	dispatch_wakeup()
	dispatch_error_queue()
end

function skynet.timeout(ti, func)
	local session = c.command("TIMEOUT",tostring(ti))
	assert(session)
	session = tonumber(session)
	local co = co_create(func)
	assert(session_id_coroutine[session] == nil)
	session_id_coroutine[session] = co
end

function skynet.sleep(ti)
	local session = c.command("TIMEOUT",tostring(ti))
	assert(session)
	session = tonumber(session)
	local succ, ret = coroutine_yield("SLEEP", session)
	sleep_session[coroutine.running()] = nil
	if ret == true then
		c.trace_switch(trace_handle, session)
		return "BREAK"
	end
end

function skynet.yield()
	return skynet.sleep("0")
end

function skynet.wait()
	local session = c.genid()
	coroutine_yield("SLEEP", session)
	c.trace_switch(trace_handle, session)
	local co = coroutine.running()
	sleep_session[co] = nil
	session_id_coroutine[session] = nil
end

function skynet.register(name)
	c.command("REG", name)
end

function skynet.name(name, handle)
	c.command("NAME", name .. " " .. handle)
end

local self_handle
function skynet.self()
	if self_handle then
		return self_handle
	end
	self_handle = string_to_handle(c.command("REG"))
	return self_handle
end

function skynet.localname(name)
	return string_to_handle(c.command("QUERY", name))
end

function skynet.launch(...)
	local addr = c.command("LAUNCH", table.concat({...}," "))
	if addr then
		return string_to_handle(addr)
	end
end

function skynet.now()
	return tonumber(c.command("NOW"))
end

function skynet.starttime()
	return tonumber(c.command("STARTTIME"))
end

function skynet.exit()
	skynet.send(".launcher","lua","REMOVE",skynet.self())
	c.command("EXIT")
end

function skynet.kill(name)
	if type(name) == "number" then
		skynet.send(".launcher","lua","REMOVE",name)
		name = skynet.address(name)
	end
	c.command("KILL",name)
end

function skynet.getenv(key)
	return c.command("GETENV",key)
end

function skynet.setenv(key, value)
	c.command("SETENV",key .. " " ..value)
end

function skynet.send(addr, typename, ...)
	local p = proto[typename]
	if watching_service[addr] == false then
		error("Service is dead")
	end
	return c.send(addr, p.id, 0 , p.pack(...))
end

function skynet.cast(group, typename, ...)
	local p = proto[typename]
	if #group > 0 then
		return c.send(".cast", p.id, 0, mc(group, p.pack(...)))
	end
end

skynet.genid = assert(c.genid)
skynet.forward = assert(c.forward)

skynet.redirect = function(dest,source,typename,...)
	return c.redirect(dest, source, proto[typename].id, ...)
end

skynet.pack = assert(c.pack)
skynet.unpack = assert(c.unpack)
skynet.tostring = assert(c.tostring)

local function yield_call(service, session)
	watching_session[session] = service
	local succ, msg, sz = coroutine_yield("CALL", session)
	watching_session[session] = nil
	assert(succ, "Capture an error")
	return msg,sz
end

function skynet.call(addr, typename, ...)
	local p = proto[typename]
	if watching_service[addr] == false then
		error("Service is dead")
	end
	local session = c.send(addr, p.id , nil , p.pack(...))
	if session == nil then
		error("call to invalid address " .. tostring(addr))
	end
	return p.unpack(yield_call(addr, session))
end

function skynet.blockcall(addr, typename , ...)
	local p = proto[typename]
	c.command("LOCK")
	local session = c.send(addr, p.id , nil , p.pack(...))
	if session == nil then
		c.command("UNLOCK")
		error("call to invalid address " .. tostring(addr))
	end
	return p.unpack(yield_call(addr, session))
end

function skynet.rawcall(addr, typename, msg, sz)
	local p = proto[typename]
	local session = assert(c.send(addr, p.id , nil , msg, sz), "call to invalid address")
	return yield_call(addr, session)
end

function skynet.ret(msg, sz)
	msg = msg or ""
	coroutine_yield("RETURN", msg, sz)
end

function skynet.wakeup(co)
	if sleep_session[co] and wakeup_session[co] == nil then
		wakeup_session[co] = true
		return true
	end
end

function skynet.dispatch(typename, func)
	local p = assert(proto[typename],tostring(typename))
	assert(p.dispatch == nil, tostring(typename))
	p.dispatch = func
end

local function unknown_response(session, address, msg, sz)
	print("Response message :" , c.tostring(msg,sz))
	error(string.format("Unknown session : %d from %x", session, address))
end

function skynet.dispatch_unknown_response(unknown)
	local prev = unknown_response
	unknown_response = unknown
	return prev
end

local fork_queue = {}

local tunpack = table.unpack

function skynet.fork(func,...)
	local args = { ... }
	local co = co_create(function()
		func(tunpack(args))
	end)
	table.insert(fork_queue, co)
end

local function raw_dispatch_message(prototype, msg, sz, session, source, ...)
	-- skynet.PTYPE_RESPONSE = 1, read skynet.h
	if prototype == 1 then
		local co = session_id_coroutine[session]
		if co == "BREAK" then
			session_id_coroutine[session] = nil
		elseif co == nil then
			unknown_response(session, source, msg, sz)
		else
			c.trace_switch(trace_handle, session)
			session_id_coroutine[session] = nil
			suspend(co, coroutine.resume(co, true, msg, sz))
		end
	else
		local p = assert(proto[prototype], prototype)
		local f = p.dispatch
		if f then
			local co = co_create(f)
			session_coroutine_id[co] = session
			session_coroutine_address[co] = source
			suspend(co, coroutine.resume(co, session,source, p.unpack(msg,sz, ...)))
		else
			print("Unknown request :" , p.unpack(msg,sz))
			error(string.format("Can't dispatch type %s : ", p.name))
		end
	end
end

local function dispatch_message(...)
	local succ, err = pcall(raw_dispatch_message,...)
	while true do
		local key,co = next(fork_queue)
		if co == nil then
			break
		end
		fork_queue[key] = nil
		local fork_succ, fork_err = pcall(suspend,co,coroutine.resume(co))
		if not fork_succ then
			if succ then
				succ = false
				err = fork_err
			else
				err = err .. "\n" .. fork_err
			end
		end
	end
	assert(succ, err)
end

function skynet.newservice(name, ...)
	local param =  table.concat({"snlua", name, ...}, " ")
	local handle = skynet.call(".launcher", "text" , param)
	if handle == "" then
		return nil
	else
		return string_to_handle(handle)
	end
end

function skynet.uniqueservice(global, ...)
	local handle
	if global == true then
		handle = skynet.call("SERVICE", "lua", "LAUNCH", ...)
	else
		handle = skynet.call(".service", "lua", "LAUNCH", global, ...)
	end
	assert(handle , "Unique service launch failed")
	return handle
end

function skynet.queryservice(global, ...)
	local handle
	if global == true then
		handle = skynet.call("SERVICE", "lua", "QUERY", ...)
	else
		handle = skynet.call(".service", "lua", "QUERY", global, ...)
	end
	assert(handle , "Unique service query failed")
	return handle
end

local function group_command(cmd, handle, address)
	if address then
		return string.format("%s %d :%x",cmd, handle, address)
	else
		return string.format("%s %d",cmd,handle)
	end
end

function skynet.enter_group(handle , address)
	c.command("GROUP", group_command("ENTER", handle, address))
end

function skynet.leave_group(handle , address)
	c.command("GROUP", group_command("LEAVE", handle, address))
end

function skynet.clear_group(handle)
	c.command("GROUP", "CLEAR " .. tostring(handle))
end

function skynet.query_group(handle)
	return string_to_handle(c.command("GROUP","QUERY " .. tostring(handle)))
end

function skynet.address(addr)
	return string.format(":%x",addr)
end

function skynet.harbor(addr)
	return c.harbor(addr)
end

----- debug

local internal_info_func

function skynet.info_func(func)
	internal_info_func = func
end

local dbgcmd = {}

function dbgcmd.MEM()
	local kb, bytes = collectgarbage "count"
	skynet.ret(skynet.pack(kb,bytes))
end

function dbgcmd.GC()
	coroutine_pool = {}
	collectgarbage "collect"
end

local function query_state(stat, what)
	stat[what] = c.stat(what)
end

function dbgcmd.STAT()
	local stat = {}
	query_state(stat, "count")
	query_state(stat, "time")
	stat.boottime = debug.getregistry().skynet_boottime
	skynet.ret(skynet.pack(stat))
end

function dbgcmd.INFO()
	if internal_info_func then
		skynet.ret(skynet.pack(internal_info_func()))
	else
		skynet.ret(skynet.pack(nil))
	end
end

function dbgcmd.RELOAD(...)
	local cmd = table.concat({...}, " ")
	c.reload(cmd)
end

local function _debug_dispatch(session, address, cmd, ...)
	local f = dbgcmd[cmd]
	assert(f, cmd)
	f(...)
end

----- register protocol
do
	local REG = skynet.register_protocol

	REG {
		name = "text",
		id = skynet.PTYPE_TEXT,
		pack = function (...)
			local n = select ("#" , ...)
			if n == 0 then
				return ""
			elseif n == 1 then
				return tostring(...)
			else
				return table.concat({...}," ")
			end
		end,
		unpack = c.tostring
	}

	REG {
		name = "lua",
		id = skynet.PTYPE_LUA,
		pack = skynet.pack,
		unpack = skynet.unpack,
	}

	REG {
		name = "response",
		id = skynet.PTYPE_RESPONSE,
	}

	REG {
		name = "debug",
		id = skynet.PTYPE_DEBUG,
		pack = skynet.pack,
		unpack = skynet.unpack,
		dispatch = _debug_dispatch,
	}

	REG {
		name = "error",
		id = skynet.PTYPE_ERROR,
		pack = skynet.pack,
		unpack = skynet.unpack,
		dispatch = _error_dispatch,
	}
end

local init_func = {}

function skynet.init(f, name)
	assert(type(f) == "function")
	if init_func == nil then
		f()
	else
		if name == nil then
			table.insert(init_func, f)
		else
			assert(init_func[name] == nil)
			init_func[name] = f
		end
	end
end

local function init_all()
	local funcs = init_func
	init_func = nil
	for k,v in pairs(funcs) do
		v()
	end
end

local function init_template(start)
	init_all()
	init_func = {}
	start()
	init_all()
end

local function init_service(start)
	local ok, err = xpcall(init_template, debug.traceback, start)
	if not ok then
		print("init service failed:", err)
		skynet.send(".launcher","text", "ERROR")
		skynet.exit()
	else
		skynet.send(".launcher","text", "")
	end
end

function skynet.start(start_func)
	c.callback(dispatch_message)
	trace_handle = assert(c.stat "trace")
	skynet.timeout(0, function()
		init_service(start_func)
	end)
end

function skynet.filter(f ,start_func)
	c.callback(function(...)
		dispatch_message(f(...))
	end)
	trace_handle = assert(c.stat "trace")
	skynet.timeout(0, function()
		init_service(start_func)
	end)
end

function skynet.trace()
	return c.trace_new(trace_handle)
end

function skynet.trace_session(session)
	return c.trace_register(trace_handle, session)
end

function skynet.trace_callback(func)
	trace_func = func
end

function skynet.endless()
	return c.command("ENDLESS")~=nil
end

function skynet.abort()
	c.command("ABORT")
end

function skynet.context_ptr()
	return c.context()
end

function skynet.monitor(service, query)
	local monitor
	if query then
		monitor = skynet.queryservice(true, service)
	else
		monitor = skynet.uniqueservice(true, service)
	end
	assert(monitor, "Monitor launch failed")
	c.command("MONITOR", string.format(":%08x", monitor))
end

return skynet
