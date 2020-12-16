local skynet = require "skynet"


skynet.start(function()
	local size = 100000
	local t = skynet.now()
	for i = 1, size do
		skynet.fork(function() end)
	end
	skynet.sleep(10)
	print(skynet.now() - t - 10)
	skynet.exit()
end)



