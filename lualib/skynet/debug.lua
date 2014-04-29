return function (skynet)

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
	skynet.ret(skynet.pack(stat))
end

function dbgcmd.INFO()
	if internal_info_func then
		skynet.ret(skynet.pack(internal_info_func()))
	else
		skynet.ret(skynet.pack(nil))
	end
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