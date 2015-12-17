local skynet = require "skynet"
-- You should use skynet.coroutine instead of origin coroutine in skynet
local coroutine = require "skynet.coroutine"

local function status(co)
	repeat
		local status = coroutine.status(co)
		print("STATUS", status)
		skynet.sleep(100)
	until status == "suspended"

	repeat
		local ok, n = assert(coroutine.resume(co))
		print("status thread", n)
	until not n
	skynet.exit()
end

local function test(n)
	local co = coroutine.running()
	print ("begin", coroutine.isskynetcoroutine(co))
	skynet.fork(status, co)
	for i=1,n do
		skynet.sleep(100)
		coroutine.yield(i)
	end
	print "end"
end

skynet.start(function()
	print("Is the main thead a skynet coroutine ?", coroutine.isskynetcoroutine())	-- always false
	print(coroutine.resume(coroutine.running()))	-- always return false
	local f = coroutine.wrap(test)
	for i=1,3 do
		local n = f(5)
		print("main thread",n)
	end
end)
