local skynet = require "skynet"

function response.ping(hello)
	skynet.sleep(100)
	return hello
end

function subscribe.hello()
	print "hello"
end

function response.error()
	error "throw an error"
end

function init( ... )
	print ("ping server start:", ...)
end

function exit(...)
	print ("ping server exit:", ...)
	return "Exit"
end
