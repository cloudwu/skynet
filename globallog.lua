local skynet = require "skynet"

skynet.callback(function(session, from , message)
	print("[GLOBALLOG]",session, from,message)
end)

skynet.command("REG","LOG")