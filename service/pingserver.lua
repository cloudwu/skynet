local skynet = require "skynet"

local command = {}

function command.PING(hello)
	skynet.ret(skynet.pack(hello))
end

function command.HELLO()
	skynet.sleep(100)
	skynet.ret(skynet.pack("hello"))
end

skynet.start(function()
	skynet.dispatch("lua", function(session,addr, cmd, ...)
		command[cmd](...)
	end)
end)
