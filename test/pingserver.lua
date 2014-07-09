local skynet = require "skynet"
local queue = require "skynet.queue"

local i = 0
local hello = "hello"

function response.ping(hello)
	skynet.sleep(100)
	return hello
end

-- response.sleep and accept.hello share one lock
local lock

function accept.sleep(queue, n)
	if queue then
		lock(
		function()
			print("queue=",queue, n)
			skynet.sleep(n)
		end)
	else
		print("queue=",queue, n)
		skynet.sleep(n)
	end
end

function accept.hello()
	lock(function()
	i = i + 1
	print (i, hello)
	end)
end

function response.error()
	error "throw an error"
end

function init( ... )
	print ("ping server start:", ...)
	-- init queue
	lock = queue()

-- You can return "queue" for queue service mode
--	return "queue"
end

function exit(...)
	print ("ping server exit:", ...)
end
