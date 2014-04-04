local skynet = require "skynet"

skynet.start(function()
	local ps = skynet.uniqueservice("pingserver")
	skynet.watch(ps)
	print(pcall(skynet.call,ps,"lua","ERROR"))
	print(skynet.call(ps, "lua", "PING", "hello"))
	print(skynet.call(ps, "lua", "PING", "hay"))
	skynet.call(ps, "lua", "EXIT")
end)
