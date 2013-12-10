local skynet = require "skynet"

local command = {}

function command.PING(hello)
	skynet.ret(skynet.pack(hello))
end

function command.HELLO()
	skynet.sleep(100)
	skynet.ret(skynet.pack("hello"))
end

function command.EXIT()
	skynet.exit()
end

function command.ERROR()
	error "throw an error"
end

skynet.start(function()
	skynet.dispatch("lua", function(session,addr, cmd, ...)
		command[cmd](...)
	end)
end)
