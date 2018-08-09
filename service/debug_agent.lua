local skynet = require "skynet"
local debugchannel = require "skynet.debugchannel"

local CMD = {}

local channel

function CMD.start(address, fd)
	assert(channel == nil, "start more than once")
	skynet.error(string.format("Attach to :%08x", address))
	local handle
	channel, handle = debugchannel.create()
	local ok, err = pcall(skynet.call, address, "debug", "REMOTEDEBUG", fd, handle)
	if not ok then
		skynet.ret(skynet.pack(false, "Debugger attach failed"))
	else
		-- todo hook
		skynet.ret(skynet.pack(true))
	end
	skynet.exit()
end

function CMD.cmd(cmdline)
	channel:write(cmdline)
end

function CMD.ping()
	skynet.ret()
end

skynet.start(function()
	skynet.dispatch("lua", function(_,_,cmd,...)
		local f = CMD[cmd]
		f(...)
	end)
end)
