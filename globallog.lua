local skynet = require "skynet"

skynet.dispatch(function(message, session , from)
	print("[GLOBALLOG]", from,message)
end)

skynet.register "LOG"