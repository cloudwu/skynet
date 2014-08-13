local skynet = require "skynet"
local harbor = require "skynet.harbor"

skynet.start(function()
	print("wait for harbor 2")
	print("run skynet examples/config_log please")
	harbor.connect(2)
	print("harbor 2 connected")
	print("LOG =", skynet.address(harbor.queryname "LOG"))
	harbor.link(2)
	print("disconnected")
end)
