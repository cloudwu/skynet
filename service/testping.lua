local skynet = require "skynet"

skynet.start(function()
	local ps = skynet.uniqueservice("pingserver")
	skynet.watch(ps)
	print(pcall(skynet.call,ps,"lua","ERROR"))
	print(skynet.call(ps, "lua", "PING", "hello"))
	skynet.send(ps, "lua", "EXIT")
	print(skynet.call(ps, "lua", "PING", "hay"))
end)
