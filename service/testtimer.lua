local skynet = require "skynet"

-- register a dummy callback function
skynet.dispatch()

skynet.start(function()
	for i = 1, 10 do
		print(i)
		skynet.sleep(100)
	end
	skynet.exit()
	print("Test timer exit")
end)
