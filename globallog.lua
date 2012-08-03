local skynet = require "skynet"

skynet.callback(function(from , message)
	print("[GLOBALLOG]",from,message)
end)

skynet.command("REG","LOG")