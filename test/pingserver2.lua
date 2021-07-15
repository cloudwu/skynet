local skynet = require "skynet"
local queue = require "skynet.queue2"
local snax = require "skynet.snax"

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
		local scope_lock<close> = lock()
		print("queue=",queue, n)
		skynet.sleep(n)
	else
		print("queue=",queue, n)
		skynet.sleep(n)
	end
end

function accept.hello()
	local scope_lock<close> = lock()
	i = i + 1
	print (i, hello)
end

function accept.exit(...)
	snax.exit(...)
end

function response.error()
	error "throw an error"
end

function init( ... )
	print ("ping server start:", ...)
	snax.enablecluster()	-- enable cluster call
	-- init queue
	lock = queue()
end

function exit(...)
	print ("ping server exit:", ...)
end
