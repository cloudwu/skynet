local skynet = require "skynet"

skynet.dispatch(function(message, from, session)
	print("[GLOBALLOG]",session, from,message)
end)

skynet.register "LOG"