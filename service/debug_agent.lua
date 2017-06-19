local skynet = require "skynet"
local debugchannel = require "debugchannel"

local CMD = {}

local channel

function CMD.start(address, fd)
	assert(channel == nil, "start more than once")
	skynet.error(string.format("Attach to :%08x", address))
	local handle
	channel, handle = debugchannel.create()
	skynet.call(address, "debug", "REMOTEDEBUG", fd, handle)
	-- todo hook
	skynet.ret(skynet.pack(nil))
	skynet.exit()
end

function CMD.cmd(cmdline)
	channel:write(cmdline)
end

skynet.start(function()
	skynet.dispatch("lua", function(_,_,cmd,...)
		local f = CMD[cmd]
		f(...)
	end)
end)
