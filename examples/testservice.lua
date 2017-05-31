local skynet = require "skynet"
local queue = require "skynet.queue"
local snax = require "snax"

local i = 0
local hello = "hello"

function response.ping(hello)
	skynet.sleep(100)
	return hello
end

function accept.hello(str)
	print("22222"..str)
end

function accept.reloadCode()
	print("Hotfix (i) :", snax.hotfix(snax.self(), [[

local i
local hello

function accept.hello(str)
	print("MMMMMMMMMMMMMM"..(i+1))
	print("33333"..str)
end

function hotfix(...)
	local temp = i
	i = 100
	return temp
end

	]]))
end

function init( ... )
	print("----------testservice start--------------")
end

function exit(...)
	print ("service exit", ...)
end
