local skynet = require "skynet"

skynet.start(function()
	skynet.dispatch("text", function(session, address, text)
		print("[GLOBALLOG]", skynet.address(address),text)
	end)
	skynet.register "LOG"
end)
