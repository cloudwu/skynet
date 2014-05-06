local skynet = require "skynet"

local mode = ...

if mode == "test" then

skynet.start(function()
	skynet.dispatch("lua", function (...)
		print("====>", ...)
		skynet.exit()
	end)
end)

else

	skynet.start(function()
		local test = skynet.newservice("testdeadcall", "test")	-- launch self in test mode

		print(pcall(function()
			skynet.send(test,"lua", "hello world")
			skynet.send(test,"lua", "never get there")
			skynet.call(test,"lua", "fake call")
		end))

		skynet.exit()
	end)
end