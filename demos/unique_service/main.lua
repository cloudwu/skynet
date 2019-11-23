local skynet = require "skynet"

skynet.start(function()
	skynet.uniqueservice("mydb")	-- uniqueservice(name, ...) 启动一个唯一服务，如果服务该服务已经启动，则返回已启动的服务地址
	skynet.uniqueservice("mydb")	-- 第二次创建,使用第一次创建的实例

	local res = skynet.call("MyDB", "lua", "set", "name", "Lily")
	print("last name", res)

	local res = skynet.call("MyDB", "lua", "set", "name", "Tommy")
	print("last name", res)

	local res = skynet.call("MyDB", "lua", "set", "hoby", {"singing", "swimming"})
	print("last hoby", res)

	local res = skynet.call("MyDB", "lua", "get", "hoby")
	print("get hoby", table.concat(res, ","))

	skynet.exit()	-- 退出当前服务
end)
