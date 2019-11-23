local skynet = require "skynet"
local socket = require "skynet.socket"

skynet.start(function()
	--　启动agent处理收到的请求，可以改成多个
	local agent = skynet.newservice("agent")

	--　监听socket
	local id = socket.listen("0.0.0.0", 8001)
	skynet.error("listen ok :8001")

	-- 开始接收数据
	socket.start(id , function(cid, addr)
		-- 转发给agent处理
		skynet.send(agent, "lua", cid)
	end)
end)

