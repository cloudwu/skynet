local skynet = require "skynet"
local sprotoloader = require "sprotoloader"

local max_client = 64

skynet.start(function()
	skynet.error("Server start")
	skynet.uniqueservice("protoloader")
	if not skynet.getenv "daemon" then
		local console = skynet.newservice("console")
	end
	skynet.newservice("debug_console",8000)
	skynet.newservice("simpledb")
	local watchdog = skynet.newservice("watchdog")

	-- 向watchdog发送一个 lua类型的消息 cmd为start, 执行watchdog的CMD.start()
	skynet.call(watchdog, "lua", "start", {
		port = 8888,  -- 最多允许 1024 个外部连接同时建立
		maxclient = max_client, -- 最多允许 max_client 个外部连接同时建立
		nodelay = true, -- 给外部连接设置  TCP_NODELAY 属性
	})

	skynet.error("Watchdog listen on", 8888)
	skynet.exit()
end)
