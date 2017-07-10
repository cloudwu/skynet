local skynet = require "skynet"
require "skynet.manager"
require "skynet.debug"

skynet.register_protocol {
	name = "text",
	id = skynet.PTYPE_TEXT,
	unpack = skynet.tostring,
	pack = function(text) return text end,
}

local socket_fd_addr = {}
local socket_addr_fd = {}
local socket_init = {}

local function close_agent(addr)
	local fd = assert(socket_addr_fd[addr])
	socket_fd_addr[fd] = nil
	socket_addr_fd[addr] = nil
end

local function subscribe(fd)
	local addr = socket_fd_addr[fd]
	if addr then
		return addr
	end
	addr = assert(skynet.launch("package", skynet.self(), fd))
	socket_fd_addr[fd] = addr
	socket_addr_fd[addr] = fd
	socket_init[addr] = skynet.response()
end

local function get_status(addr)
	local ok, info = pcall(skynet.call,addr, "text", "I")
	if ok then
		return info
	else
		return "EXIT"
	end
end

skynet.info_func(function()
	local tmp = {}
	for fd,addr in pairs(socket_fd_addr) do
		if socket_init[addr] then
			table.insert(tmp, { fd = fd, addr = skynet.address(addr), status = "NOTREADY" })
		else
			table.insert(tmp, { fd = fd, addr = skynet.address(addr), status = get_status(addr) })
		end
	end
	return tmp
end)

skynet.start(function()
	skynet.dispatch("text", function (session, source, cmd)
		if cmd == "CLOSED" then
			close_agent(source)
		elseif cmd == "SUCC" then
			socket_init[source](true, source)
			socket_init[source] = nil
		elseif cmd == "FAIL" then
			socket_init[source](false)
			socket_init[source] = nil
		else
			skynet.error("Invalid command " .. cmd)
		end
	end)
	skynet.dispatch("lua", function (session, source, fd)
		assert(type(fd) == "number")
		local addr = subscribe(fd)
		if addr then
			skynet.ret(skynet.pack(addr))
		end
	end)
end)
