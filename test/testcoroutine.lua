local skynet = require "skynet"
-- You should use skynet.coroutine instead of origin coroutine in skynet
local coroutine = require "skynet.coroutine"

local function test(n)
	print ("begin", coroutine.isskynetcoroutine())
	for i=1,n do
		skynet.sleep(100)
		coroutine.yield(i)
	end
	print "end"
	return false
end

skynet.start(function()
	print("Is the main thead a skynet coroutine ?", coroutine.isskynetcoroutine(coroutine.running()))	-- always false
	print(coroutine.resume(coroutine.running()))	-- always return false
	local f = coroutine.wrap(test)
	repeat
		local n = f(5)
		print(n)
	until not n
	skynet.exit()
end)
