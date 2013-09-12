local skynet = require "skynet"

local function timeout(t)
	print(t)
end

local function wakeup(co)
	for i=1,5 do
		skynet.sleep(50)
		skynet.wakeup(co)
	end
end

skynet.start(function()
	skynet.fork(wakeup, coroutine.running())
	skynet.timeout(300, function() timeout "Hello World" end)
	for i = 1, 10 do
		print(i, skynet.now())
		print(skynet.sleep(100))
	end
	skynet.exit()
	print("Test timer exit")
end)
