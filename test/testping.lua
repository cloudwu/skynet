local skynet = require "skynet"
local snax = require "snax"

skynet.start(function()
	local ps = snax.newservice ("pingserver", "hello world")
	print(ps.pub.hello())
	print(ps.req.ping("foobar"))
	print(pcall(ps.req.error))
	snax.hotfix(ps)
	print(ps.pub.hello())
	print(snax.kill(ps,"exit"))
	skynet.exit()
end)
