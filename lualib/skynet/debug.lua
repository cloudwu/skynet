local table = table

return function (skynet, export)

local internal_info_func

function skynet.info_func(func)
	internal_info_func = func
end

local dbgcmd

local function init_dbgcmd()
dbgcmd = {}

function dbgcmd.MEM()
	local kb, bytes = collectgarbage "count"
	skynet.ret(skynet.pack(kb,bytes))
end

function dbgcmd.GC()
	export.clear()
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
	local output = inject(skynet, source, filename , export.dispatch, skynet.register_protocol)
	collectgarbage "collect"
	skynet.ret(skynet.pack(table.concat(output, "\n")))
end

function dbgcmd.TERM(service)
	skynet.term(service)
end

function dbgcmd.REMOTEDEBUG(...)
	local remotedebug = require "skynet.remotedebug"
	remotedebug.start(export, ...)
end

function dbgcmd.SUPPORT(pname)
	return skynet.ret(skynet.pack(skynet.dispatch(pname) ~= nil))
end

return dbgcmd
end -- function init_dbgcmd

local function _debug_dispatch(session, address, cmd, ...)
	local f = (dbgcmd or init_dbgcmd())[cmd]	-- lazy init dbgcmd
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
