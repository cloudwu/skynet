local skynet = require "skynet"

local i = 0
local hello = "hello"

function response.ping(hello)
	skynet.sleep(100)
	return hello
end

function accept.hello()
	i = i + 1
	print (i, hello)
end

function response.error()
	error "throw an error"
end

function init( ... )
	print ("ping server start:", ...)

-- You can return "queue" for queue service mode
--	return "queue"
end

function exit(...)
	print ("ping server exit:", ...)
end
