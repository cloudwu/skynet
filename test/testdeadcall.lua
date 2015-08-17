local skynet = require "skynet"

local mode = ...

if mode == "test" then

skynet.start(function()
	skynet.dispatch("lua", function (...)
		print("====>", ...)
		skynet.exit()
	end)
end)

elseif mode == "dead" then

skynet.start(function()
	skynet.dispatch("lua", function (...)
		skynet.sleep(100)
		print("return", skynet.ret "")
	end)
end)

else

	skynet.start(function()
		local test = skynet.newservice(SERVICE_NAME, "test")	-- launch self in test mode

		print(pcall(function()
			skynet.call(test,"lua", "dead call")
		end))

		local dead = skynet.newservice(SERVICE_NAME, "dead")	-- launch self in dead mode

		skynet.timeout(0, skynet.exit)	-- exit after a while, so the call never return
		skynet.call(dead, "lua", "whould not return")
	end)
end
