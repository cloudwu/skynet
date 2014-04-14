local skynet = require "skynet"
local jsonpack = require "jsonpack"
local netpack = require "netpack"
local socket = require "socket"

local CMD = {}

local client_fd

local function send_client(v)
	socket.write(client_fd, netpack.pack(jsonpack.pack(0,v)))
end

local function response_client(session,v)
	socket.write(client_fd, netpack.pack(jsonpack.response(session,v)))
end

skynet.register_protocol {
	name = "client",
	id = skynet.PTYPE_CLIENT,
	unpack = function (msg, sz)
		return jsonpack.unpack(skynet.tostring(msg,sz))
	end,
	dispatch = function (_, _, session, args)
		local ok, result = pcall(skynet.call,"SIMPLEDB", "lua", table.unpack(args))
		if ok then
			response_client(session, { true, result })
		else
			response_client(session, { false, "Invalid command" })
		end
	end
}

function CMD.start(gate , fd)
	client_fd = fd
	skynet.call(gate, "lua", "forward", fd)
	send_client "Welcome to skynet"
end

skynet.start(function()
	skynet.dispatch("lua", function(_,_, command, ...)
		local f = CMD[command]
		skynet.ret(skynet.pack(f(...)))
	end)
end)
