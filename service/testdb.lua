local skynet = require "skynet"

-- register a dummy callback function
skynet.dispatch()

skynet.start(function()
	local result = skynet.call("SIMPLEDB","SET foobar hello")
	print(result)
	result = skynet.call("SIMPLEDB","GET foobar")
	print(result)
	skynet.exit()
end)
