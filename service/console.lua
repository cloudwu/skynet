local skynet = require "skynet"
local snax	 = require "snax"
local socket = require "socket"

local function console_main_loop()
	local stdin = socket.stdin()
	socket.lock(stdin)
	while true do
		local cmdline = socket.readline(stdin, "\n")
		local i,j = cmdline:find("^snax%s+%w")
		if i then
			pcall(snax.newservice, cmdline:sub(j))
		elseif cmdline ~= "" then
			pcall(skynet.newservice,cmdline)
		end
	end
	socket.unlock(stdin)
end

skynet.start(function()
	skynet.fork(console_main_loop)
end)
