
--- 一个通用模板 lualib/snax/gateserver.lua 来启动一个网关服务器，通过 TCP 连接和客户端交换数据
--- 这个模板不可以和 Socket 库一起使用。因为这个模板接管了 socket 类的消息
--- TCP粘包: TCP 基于数据流，但一般我们需要以带长度信息的数据包的结构来做数据交换。gateserver 做的就是这个工作，把数据流切割成包的形式转发到可以处理它的地址

--- service/gate.lua 是一个实现完整的网关服务器，同时也可以作为 snax.gateserver 的使用范例
--- examples/watchdog.lua 是一个可以参考的例子，它启动了一个 service/gate.lua 服务，并将处理外部连接的消息转发处理

local skynet = require "skynet"
local netpack = require "skynet.netpack"
local socketdriver = require "skynet.socketdriver"

local gateserver = {}

local socket	-- listen socket
local queue		-- message queue
local maxclient	-- max client
local client_number = 0
local CMD = setmetatable({}, { __gc = function() netpack.clear(queue) end })
local nodelay = false

local connection = {}


-- 每次收到 handler.connect 后，你都需要调用 openclient 让 fd 上的消息进入。
-- 默认状态下， fd 仅仅是连接上你的服务器，但无法发送消息给你。
-- 这个步骤需要你显式的调用是因为，或许你需要在新连接建立后，把 fd 的控制权转交给别的服务。那么你可以在一切准备好以后，再放行消息。
function gateserver.openclient(fd)
	if connection[fd] then
		socketdriver.start(fd)
	end
end
-- 用于主动踢掉一个连接
function gateserver.closeclient(fd)
	local c = connection[fd]
	if c then
		connection[fd] = false
		socketdriver.close(fd)
	end
end

-- 注册网络事件处理
function gateserver.start(handler)
	-- 这两个事件必须要有
	assert(handler.message)
	assert(handler.connect)

	-- 处理接入，accpet之后被触发
	function CMD.open( source, conf )
		assert(not socket)
		local address = conf.address or "0.0.0.0"
		local port = assert(conf.port)
		maxclient = conf.maxclient or 1024
		nodelay = conf.nodelay
		skynet.error(string.format("Listen on %s:%d", address, port))

		-- 真正处理网络部分。socketdriver就是处理socket的服务节点 见 lua-socket.c
		socket = socketdriver.listen(address, port)
		socketdriver.start(socket)
		--- 如果上层应用有open指令，那么执行之
		if handler.open then
			return handler.open(source, conf)
		end
	end

	-- 处理接入，断开之后被触发
	function CMD.close()
		assert(socket)
		socketdriver.close(socket)
	end

	local MSG = {}

	-- 收到数据的消息处理，注意是已经去掉长度头部组装好的数据帧
	local function dispatch_msg(fd, msg, sz)
		if connection[fd] then
			handler.message(fd, msg, sz)
		else
			skynet.error(string.format("Drop message from fd (%d) : %s", fd, netpack.tostring(msg,sz)))
		end
	end

	MSG.data = dispatch_msg

	local function dispatch_queue()
		local fd, msg, sz = netpack.pop(queue)
		if fd then
			-- may dispatch even the handler.message blocked
			-- If the handler.message never block, the queue should be empty, so only fork once and then exit.
			skynet.fork(dispatch_queue)
			dispatch_msg(fd, msg, sz)

			for fd, msg, sz in netpack.pop, queue do
				dispatch_msg(fd, msg, sz)
			end
		end
	end

	MSG.more = dispatch_queue

	function MSG.open(fd, msg)
		if client_number >= maxclient then
			socketdriver.close(fd)
			return
		end
		if nodelay then
			socketdriver.nodelay(fd)
		end
		connection[fd] = true
		client_number = client_number + 1
		handler.connect(fd, msg)
	end

	local function close_fd(fd)
		local c = connection[fd]
		if c ~= nil then
			connection[fd] = nil
			client_number = client_number - 1
		end
	end

	function MSG.close(fd)
		if fd ~= socket then
			if handler.disconnect then
				handler.disconnect(fd)
			end
			close_fd(fd)
		else
			socket = nil
		end
	end

	function MSG.error(fd, msg)
		if fd == socket then
			socketdriver.close(fd)
			skynet.error("gateserver close listen socket, accpet error:",msg)
		else
			if handler.error then
				handler.error(fd, msg)
			end
			close_fd(fd)
		end
	end

	function MSG.warning(fd, size)
		if handler.warning then
			handler.warning(fd, size)
		end
	end

	-- gateserver被加载时，会注册对”socket”(PTYPE_SOCKET)类型消息的处理
	-- socket消息被netpack中间件filter()处理转换为其他消息类型
	skynet.register_protocol {
		name = "socket",
		id = skynet.PTYPE_SOCKET,	-- PTYPE_SOCKET = 6
		unpack = function ( msg, sz )
			return netpack.filter( queue, msg, sz) -- netpack.filter对收到的数据进行分包
		end,
		dispatch = function (_, _, q, type, ...)
			queue = q
			if type then
				MSG[type](...)
			end
		end
	}


	-- 这里是处理lua类型消息的地方
	skynet.start(function()
		skynet.dispatch("lua", function (_, address, cmd, ...)
			local f = CMD[cmd]
			if f then
				skynet.ret(skynet.pack(f(address, ...)))
			else
				skynet.ret(skynet.pack(handler.command(cmd, address, ...)))
			end
		end)
	end)
end

return gateserver
