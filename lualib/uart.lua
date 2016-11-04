local driver = require "uartdriver"
local skynet = require "skynet"
local skynet_core = require "skynet.core"
local assert = assert

local uart = {}	-- api
local uart_pool = setmetatable ( -- store all socket object
{},
{ __gc = function (p)
		for id, v in pairs (p) do
			driver.close (id)
			-- don't need clear v.buffer, because buffer pool will be free at the end
			p[id] = nil
		end
	end
}
)

local socket_message = {}

local function wakeup (s)
	local co = s.co
	if co then
		s.co = nil
		skynet.wakeup (co)
	end
end

-- read skynet_socket.h for these macro
-- SKYNET_SOCKET_TYPE_DATA = 1
socket_message[1] = function (id, size, data)
	local s = uart_pool[id]
	if s == nil or s.callback == nil then
		skynet.error("uart: drop uart package from " .. id)
		driver.drop(data, size)
		return
	end
	local str = skynet.tostring(data, size)
	skynet_core.trash(data, size)
	s.callback(id,str)
end

-- SKYNET_SOCKET_TYPE_CONNECT = 2
socket_message[2] = function (id, _, addr)
	local s = uart_pool[id]
	if s == nil then
		return
	end
	-- log remote addr
	s.connected = true
	wakeup (s)
end

-- SKYNET_SOCKET_TYPE_CLOSE = 3
socket_message[3] = function (id)
	local s = uart_pool[id]
	if s == nil then
		return
	end
	s.connected = false
	wakeup (s)
end

skynet.register_protocol {
	name = "socket",
	id = skynet.PTYPE_SOCKET,	-- PTYPE_SOCKET = 6
	unpack = driver.unpack,
	dispatch = function (_, _, t, ...)
		socket_message[t](...)
	end
}

local function create_uart_object(id, cb)
	assert(not uart_pool[id], "uart is not closed")
	uart_pool[id] = {
		id = id,
		connected = false,
		protocol = "UART",
		callback = cb,
	}
end

function uart.open(callback,port)
	local id = driver.open(port)
	create_uart_object(id, callback)
	return id
end

uart.send = assert(driver.send)
uart.set = driver.set

return uart