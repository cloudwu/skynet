local skynet = require "skynet"

skynet.callback(function(session,addr,content)
	print("sn:",session)
	skynet.command("TIMEOUT", -1 , "100")
end)

skynet.command("TIMEOUT",0,"0")