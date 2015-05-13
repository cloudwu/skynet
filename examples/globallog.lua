local skynet = require "skynet"
require "skynet.manager"	-- import skynet.register

skynet.start(function()
	skynet.dispatch("lua", function(session, address, ...)
		print("[GLOBALLOG]", skynet.address(address), ...)
	end)
	skynet.register ".log"
	skynet.register "LOG"
end)
