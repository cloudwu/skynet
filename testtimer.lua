local skynet = require "skynet"

skynet.callback(function(addr,content)
	if content == nil then
		print("sn:",addr)
		skynet.command("TIMEOUT","100:"..tostring(addr+1))
	end
end)

skynet.command("TIMEOUT","0:0")