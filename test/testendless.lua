local skynet = require "skynet"

skynet.start(function()
	for i = 1, 1000000000 do	-- very long loop
		if i%100000000 == 0 then
			print("Endless = ", skynet.stat "endless")
			print("Cost time = ", skynet.stat "time")
		end
	end
	skynet.exit()
end)
