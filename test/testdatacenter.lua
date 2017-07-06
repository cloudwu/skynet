local skynet = require "skynet"
local datacenter = require "skynet.datacenter"

local function f1()
	print("====1==== wait hello")
	print("\t1>",datacenter.wait ("hello"))
	print("====1==== wait key.foobar")
	print("\t1>", pcall(datacenter.wait,"key"))	-- will failed, because "key" is a branch
	print("\t1>",datacenter.wait ("key", "foobar"))
end

local function f2()
	skynet.sleep(10)
	print("====2==== set key.foobar")
	datacenter.set("key", "foobar", "bingo")
end

skynet.start(function()
	datacenter.set("hello", "world")
	print(datacenter.get "hello")
	skynet.fork(f1)
	skynet.fork(f2)
end)
