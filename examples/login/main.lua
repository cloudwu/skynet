local skynet = require "skynet"

skynet.start(function()
	local loginserver = skynet.newservice("logind")
	local gate = skynet.newservice("gated", loginserver) -- gate 服务启动
	-- 立刻开始监听  通过lua协议向gate发送一个open指令，附带一个启动参数表
	skynet.call(gate, "lua", "open" , {
		port = 8888,    -- 监听端口 8888
		maxclient = 64, -- 最多允许 1024 个外部连接同时建立
		servername = "sample",
	})
end)
