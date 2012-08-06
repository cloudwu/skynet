local skynet = require "skynet"

print("agent",...)

skynet.dispatch(function(msg , addr)
	print("[agent]",addr,msg)
end)
