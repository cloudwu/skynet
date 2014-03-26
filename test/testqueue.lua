local skynet = require "skynet"
local mqueue = require "mqueue"

skynet.start(function()
	local pingqueue = skynet.newservice "pingqueue"
	print(mqueue.call(pingqueue, "A"))
	print(mqueue.call(pingqueue, "B"))
	print(mqueue.call(pingqueue, "C"))
	skynet.exit()
end)
