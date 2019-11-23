local skynet = require "skynet"
local mc = require "skynet.multicast"

local mode = ...

local function consumer()
	local dispatchs_message = function (channel, source, ...)
		print(string.format("%s <=== %s %s", skynet.address(skynet.self()), skynet.address(source), channel))
		print(channel, source, ...)
	end

	skynet.start(function()
		skynet.dispatch("lua", function (_,_, cmd, channel)
			assert(cmd == "init")
			local c = mc.new {
				channel = channel ,				-- 绑定上频道
				dispatch = dispatchs_message,	-- 设置这个频道的消息处理函数
			}

			print(skynet.address(skynet.self()), "sub", c)

			c:subscribe()		--　订阅

			skynet.ret(skynet.pack())
		end)
	end)
end

local function producer()
	skynet.start(function()
		local channel = mc.new()	-- -- 创建一个频道，成功创建后，.channel 是这个频道的 id

		for i = 1, 10 do
			local sub = skynet.newservice(SERVICE_NAME, "sub")			-- 启动新服务
			skynet.call(sub, "lua", "init", channel.channel)	--　新服务订阅广播
		end

		print(skynet.address(skynet.self()), "===>", channel)
		channel:publish("Hello World " .. os.time())					--　发送广播

		channel:delete()	-- 删除频道
	end)
end

if mode == "sub" then
	consumer()
else
	producer()
end