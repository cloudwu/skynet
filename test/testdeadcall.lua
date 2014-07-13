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
		local test = skynet.newservice(SERVICE_NAME, "test")	-- launch self in test mode

		print(pcall(function()
			skynet.call(test,"lua", "dead call")
		end))

		skynet.exit()
	end)
end