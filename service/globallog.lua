local skynet = require "skynet"

skynet.dispatch(function(msg, sz , session , from)
	local message = skynet.tostring(msg,sz)
	print("[GLOBALLOG]", skynet.address(from),message)
end)

skynet.register "LOG"