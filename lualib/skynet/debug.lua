local io = io
local table = table
local debug = debug

return function (skynet, dispatch_func)

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

function dbgcmd.STAT()
	local stat = {}
	stat.mqlen = skynet.mqlen()
	stat.task = skynet.task()
	skynet.ret(skynet.pack(stat))
end

function dbgcmd.TASK()
	local task = {}
	skynet.task(task)
	skynet.ret(skynet.pack(task))
end

function dbgcmd.INFO()
	if internal_info_func then
		skynet.ret(skynet.pack(internal_info_func()))
	else
		skynet.ret(skynet.pack(nil))
	end
end

function dbgcmd.EXIT()
	skynet.exit()
end

local function getupvaluetable(u, func, unique)
	i = 1
	while true do
		local name, value = debug.getupvalue(func, i)
		if name == nil then
			return
		end
		local t = type(value)
		if t == "table" then
			u[name] = value
		elseif t == "function" then
			if not unique[value] then
				unique[value] = true
				getupvaluetable(u, value, unique)
			end
		end
		i=i+1
	end
end

function dbgcmd.RUN(source, filename)
	if filename then
		filename = "@" .. filename
	else
		filename = "=(load)"
	end
	local output = {}
	do
		local function print(...)
			local value = { ... }
			for k,v in ipairs(value) do
				value[k] = tostring(v)
			end
			table.insert(output, table.concat(value, "\t"))
		end
		local u = {}
		local unique = {}
		getupvaluetable(u, dispatch_func, unique)
		getupvaluetable(u, skynet.register_protocol, unique)
		local env = setmetatable( { print = print , _U = u }, { __index = _ENV })
		local func, err = load(source, filename, "bt", env)
		if not func then
			skynet.ret(skynet.pack(err))
			return
		end
		local ok, err = pcall(func)
		if not ok then
			table.insert(output, err)
		end

		source, filename = nil
	end
	collectgarbage "collect"
	skynet.ret(skynet.pack(table.concat(output, "\n")))
end

local function _debug_dispatch(session, address, cmd, ...)
	local f = dbgcmd[cmd]
	assert(f, cmd)
	f(...)
end

skynet.register_protocol {
	name = "debug",
	id = assert(skynet.PTYPE_DEBUG),
	pack = assert(skynet.pack),
	unpack = assert(skynet.unpack),
	dispatch = _debug_dispatch,
}

end
