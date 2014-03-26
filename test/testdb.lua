local skynet = require "skynet"

skynet.start(function()
	local result = skynet.call("SIMPLEDB","text","SET","foobar","hello")
	print(result)
	result = skynet.call("SIMPLEDB","text","GET","foobar")
	print(result)
	skynet.exit()
end)
