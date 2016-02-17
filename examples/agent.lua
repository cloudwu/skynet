local skynet = require "skynet"
local netpack = require "netpack"
local socket = require "socket"
local sproto = require "sproto"
local sprotoloader = require "sprotoloader"
local json = require "cjson"

local WATCHDOG
local host
local send_request

local CMD = {}
local REQUEST = {}
local client_fd

function REQUEST.register(tab)
	print("register account=" .. tab.account .. ";pwd=" .. tab.pwd)
	local r = skynet.call("MYSQL","lua","register",tab.account,tab.pwd)
	return { result = r}
end

function REQUEST:get()
	print("get", self.what)
	local r = skynet.call("SIMPLEDB", "lua", "get", self.what)
	return { result = r }
end

function REQUEST:set()
	print("set", self.what, self.value)
	local r = skynet.call("SIMPLEDB", "lua", "set", self.what, self.value)
end

function REQUEST:handshake()
	return { msg = "Welcome to skynet, I will send heartbeat every 5 sec." }
end

function REQUEST:quit()
	skynet.call(WATCHDOG, "lua", "close", client_fd)
end

local function request(name, tab)
	print("request name=" .. tab.account)
	local f = assert(REQUEST[name])
	local r = f(tab)
	if response then
		return response(r)
	end
end

local function send_package(pack)
	local package = string.pack(">s2", pack)
	socket.write(client_fd, package)
end

skynet.register_protocol {
	name = "client",
	id = skynet.PTYPE_CLIENT,
	unpack = function (msg, sz)
		local data = netpack.tostring(msg,sz)
		print("agent recv data=" .. data)
		local tab = json.decode(string.sub(data,7))
		return tab.type,tab.name,tab
	end,
	dispatch = function (_, _, type, name,tab)
		print("type=" .. type)
		print("name=" .. name)
		print("tab="  .. tab.type)
		if type == "request" then
			local ok, result  = pcall(request, name,tab)
			if ok then
				if result then
					send_package(result)
				end
			else
				skynet.error(result)
			end
		else
			assert(type == "RESPONSE")
			error "This example doesn't support request client"
		end
	end
}

function CMD.start(conf)
	local fd = conf.client
	local gate = conf.gate
	WATCHDOG = conf.watchdog
	-- slot 1,2 set at main.lua
	--host = sprotoloader.load(1):host "package"
	--send_request = host:attach(sprotoloader.load(2))
	skynet.fork(function()
		while true do
			send_package("heartbeat")
			skynet.sleep(500)
		end
	end)

	client_fd = fd
	skynet.call(gate, "lua", "forward", fd)
end

function CMD.disconnect()
	-- todo: do something before exit
	skynet.exit()
end

skynet.start(function()
	skynet.dispatch("lua", function(_,_, command, ...)
		local f = CMD[command]
		skynet.ret(skynet.pack(f(...)))
	end)
end)
