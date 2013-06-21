local skynet = require "skynet"
local socket = require "socket"

skynet.start(function()
	local stdin = socket.stdin()
	socket.lock(stdin)
	while true do
		local cmdline = socket.readline(stdin, "\n")
		local handle = skynet.newservice(cmdline)
		if handle == nil then
			print("Launch error:",cmdline)
		end
	end
	socket.unlock(stdin)
end)
