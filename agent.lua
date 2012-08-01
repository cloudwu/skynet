local skynet = require "skynet"

print("agent",...)

skynet.callback(function(addr, msg)
	print("[agent]",addr,msg)
end)
