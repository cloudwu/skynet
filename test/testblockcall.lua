local skynet = require "skynet"

skynet.start(function()
	local ping = skynet.newservice("pingserver")
	skynet.timeout(0,function()
		print(skynet.call(ping,"lua","PING","ping"))
	end)

	print(skynet.blockcall(ping,"lua","HELLO"))
end)
