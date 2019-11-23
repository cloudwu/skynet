local skynet = require "skynet"

skynet.start(function()
	skynet.error("======================= test normal service")

	-- newservice(name, ...) 启动一个名为name的新服务. 等待到初始化函数结束才会返回
	local counter1 = skynet.newservice("counter", 1)
	local counter2 = skynet.newservice("counter", 2)		-- 新的服务

	print("counter1 address:", counter1)
	print("counter2 address:", counter2)

	-- send(addr, type, ...) 用type类型向addr发送一个消息，不等待回应
	skynet.send(counter1, "lua", "active")

	-- call(addr, type, ...) 用type类型发送一个消息到addr，并等待对方的回应
	local res = skynet.call(counter1, "lua", "current_count")
	print("call counter1: ", res)

	local res, err = pcall(skynet.call, counter1, "lua", "not exists")
	print("=========== ", res, err)

	skynet.exit()	-- 退出当前服务
end)
