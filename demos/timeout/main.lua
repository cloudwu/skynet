local skynet = require "skynet"

--　自定义interval函数
skynet.interval = function(interval, cb)
	local f
	f = function()
		cb()
		skynet.timeout(interval, f)
	end
	skynet.timeout(interval, f)
end

-- 时间精度: 0.01s
skynet.start(function()
	skynet.timeout(200, function()
		print("timeout: now: ", skynet.now())
	end)

	--　定时执行１．　fork(func, ...) 启动一个新的任务去执行函数 func　
	skynet.fork(function()
		skynet.sleep(200)	-- sleep(time) 让当前的任务等待 time*0.01s
		print("fork: now: ", skynet.now())
	end)

	-- 定时执行２
	skynet.interval(300, function()
		print("interval", skynet.now())
	end)
end)

