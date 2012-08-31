local skynet = require "skynet"
local socket = require "socket"

local function readline(sep)
	while true do
		local line = socket.readline(sep)
		if line then
			return line
		end
		coroutine.yield()
	end
end

local function split_package()
	while true do
		local cmd = readline "\n"
		if cmd ~= "" then
			local handle = skynet.launch("snlua", cmd)
			if handle == nil then
				print("Launch error:",cmd)
			end
		end
	end
end

local split_co = coroutine.create(split_package)

skynet.register_protocol {
	name = "client",
	id = 3,
	pack = function(...) return ... end,
	unpack = function(msg,sz)
		assert(msg , "Stdin closed")
		socket.push(msg,sz)
		assert(coroutine.resume(split_co))
	end,
	dispatch = function () end
}


skynet.start(function()
	socket.stdin()
end)
