local skynet = require "skynet"
local snax = require "snax"

skynet.start(function()
	local ps = snax.newservice ("pingserver", "hello world")
	print(ps.pub.hello())
	print(ps.req.ping("foobar"))
	print(pcall(ps.req.error))
	print(ps.kill("exit"))
	skynet.exit()
end)
