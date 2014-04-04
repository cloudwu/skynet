local skynet = require "skynet"

local command = {}

function command.PING(hello)
	return hello
end

function command.HELLO()
	skynet.sleep(100)
	return "hello"
end

function command.EXIT()
	skynet.exit()
end

function command.ERROR()
	error "throw an error"
end


skynet.start(function()
	skynet.dispatch("lua", function(session,addr, cmd, ...)
		skynet.ret(skynet.pack(command[cmd](...)))
	end)
end)
