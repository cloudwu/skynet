local skynet = require "skynet"

-- register a dummy callback function
skynet.dispatch()

local function timeout(t)
	print(t)
end

skynet.start(function()
	skynet.timeout(500, timeout, "Hello World")
	for i = 1, 10 do
		print(i)
		skynet.sleep(100)
	end
	skynet.exit()
	print("Test timer exit")
end)
