--- 一个通用模板 lualib/snax/gateserver.lua 来启动一个网关服务器，通过 TCP 连接和客户端交换数据
--- service/gate.lua 是一个实现完整的网关服务器，同时也可以作为 snax.gateserver 的使用范例


--- examples/watchdog.lua 是一个可以参考的例子，它启动了一个 service/gate.lua 服务，并将处理外部连接的消息转发处理

local skynet = require "skynet"

local CMD = {}
local SOCKET = {}
local gate
local agent = {}


-- 看门狗建立了agent并通过start指令将所有信息转交给它
function SOCKET.open(fd, addr)
	skynet.error("New client from : " .. addr)
	agent[fd] = skynet.newservice("agent") -- 为gate的连接创建agent
	skynet.call(agent[fd], "lua", "start", { gate = gate, client = fd, watchdog = skynet.self() }) --start agent
end

local function close_agent(fd)
	local a = agent[fd]
	agent[fd] = nil
	if a then
		skynet.call(gate, "lua", "kick", fd)
		-- disconnect never return
		skynet.send(a, "lua", "disconnect")
	end
end

function SOCKET.close(fd)
	print("socket close",fd)
	close_agent(fd)
end

function SOCKET.error(fd, msg)
	print("socket error",fd, msg)
	close_agent(fd)
end

function SOCKET.warning(fd, size)
	-- size K bytes havn't send out in fd
	print("socket warning", fd, size)
end

-- 在gate在收到数据时被传递触发
function SOCKET.data(fd, msg)
end

function CMD.start(conf)
	skynet.call(gate, "lua", "open" , conf) -- 在外部向你定义的gate服务发送启动消息，并传入启动配置(端口，最大连接数等)来启动gate服务。
end

function CMD.close(fd)
	close_agent(fd)
end

skynet.start(function()
	-- 调用skynet.dispatch函数注册      也可以调用skynet.register_protocol注册
	skynet.dispatch("lua",
		function(session, source, cmd, subcmd, ...)
			if cmd == "socket" then
				local f = SOCKET[subcmd]
				f(...)
				-- socket api don't need return
			else
				local f = assert(CMD[cmd])
				skynet.ret(skynet.pack(f(subcmd, ...))) -- subcmd is port, ... are maxclient nodelay
			end
		end
	)
	gate = skynet.newservice("gate")
end)
