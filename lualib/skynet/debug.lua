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

function dbgcmd.RUN(source, filename)
	local inject = require "skynet.inject"
	local output = inject(source, filename , dispatch_func, skynet.register_protocol)
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
