local skynet = require "skynet"
local snax = require "snax"

skynet.start(function()
	local ps = snax.newservice ("pingserver", "hello world")
	print(ps.req.ping("foobar"))
	print(ps.pub.hello())
	print(pcall(ps.req.error))
	print("Hotfix (i) :", snax.hotfix(ps, [[

local i
local hello

function subscribe.hello()
	i = i + 1
	print ("fix", i, hello)
end

function hotfix(...)
	local temp = i
	i = 100
	return temp
end

	]]))
	print(ps.pub.hello())
	print(snax.kill(ps,"exit"))
	skynet.exit()
end)
