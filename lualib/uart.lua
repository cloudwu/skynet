local driver = require "uartdriver"
local skynet = require "skynet"
local skynet_core = require "skynet.core"
local assert = assert

local uart = {}	-- api
local uart_pool = {}

local uart_message = {}

local function wakeup(s)
	local co = s.co
	if co then
		s.co = nil
		skynet.wakeup(co)
	end
end

local function suspend(s)
	assert(not s.co)
	s.co = coroutine.running()
	skynet.wait(s.co)
	if s.closing then
		skynet.wakeup(s.closing)
	end
end

-- SKYNET_SOCKET_TYPE_DATA = 1
uart_message[1] = function (id, size, data)
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
uart_message[2] = function(id, _ , _)
	local s = uart_pool[id]
	if s == nil then
		skynet.error("uart: no open uart")
		return
	end
	wakeup(s)
end

-- SKYNET_SOCKET_TYPE_CLOSE = 3
uart_message[3] = function (id)
	local s = uart_pool[id]
	if s == nil then
		skynet.error("uart: no open uart")
		return
	end
	uart_pool[id] = nil
end

skynet.register_protocol {
	name = "socket",
	id = skynet.PTYPE_SOCKET,	-- PTYPE_SOCKET = 6
	unpack = driver.unpack,
	dispatch = function (_, _, t, ...)
		uart_message[t](...)
	end
}

local function create_uart_object(id, cb)
	assert(not uart_pool[id], "uart is not close")
	local s = {
		id = id,
		connected = false,
		protocol = "UART",
		callback = cb,
		co = false,
	}
	uart_pool[id] = s
	suspend(s)
end

function uart.open(callback,port)
	local id = driver.open(port)
	if id < 0 then
		return id
	end
	create_uart_object(id, callback)
	skynet.error("open "..port.." success")
	return id
end

function uart.close(id)
	local s = uart_pool[id]
	if s == nil then
		return
	end
	uart_pool[id] = nil
	driver.close(id)
end

function uart.send(id,data)
	local s = uart_pool[id]
	if s == nil then
		skynet.error("uart is not open")
		return
	end
	if not s.connected then
		skynet.error("uart should set first")
		return
	end
	driver.send(id,data)
end

function uart.set(id, speed, flow_ctrl, databits, stopbits, parity)
	local s = uart_pool[id]
		if s == nil then
		skynet.error("uart is not open")
		return
	end
	local ok = driver.set(id, speed, flow_ctrl, databits, stopbits, parity)
	if ok then
		s.connected = true
	end
end

return uart