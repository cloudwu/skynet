local skynet = require "skynet"
local socket = require "socket"

local server = "127.0.0.1:8866"

local function reconnect()
	while socket.connect(server) do
		skynet.sleep(1000)
		print("reconnect to", server)
	end
	print(server, "connected")
end

skynet.register_protocol {
	name = "client",
	id = 3,
	pack = function(...) return ... end,
	unpack = function(msg,sz)
		if sz == 0 then
			skynet.timeout(0, reconnect)
			return
		end
		print("sz=",sz,skynet.tostring(msg,sz))
	end,
	dispatch = function () end
}

skynet.start(function()
	if socket.connect(server) then
		print "use 'nc -l 8866' first to listen"
		skynet.exit()
	end
end)

