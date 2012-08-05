local skynet = require "skynet"

print("agent",...)

skynet.callback(function(session, addr, msg)
	print("[agent]",session,addr,msg)
end)
