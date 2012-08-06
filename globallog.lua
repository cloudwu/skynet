local skynet = require "skynet"

skynet.dispatch(function(message, session , from)
	print("[GLOBALLOG]",session, from,message)
end)

skynet.register "LOG"