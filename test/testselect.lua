local skynet = require "skynet"

local mode = ...

if mode == "slave" then

local COMMAND = {}

function COMMAND.ping(ti, str)
	skynet.sleep(ti)
	return str
end

function COMMAND.error()
	error "ERROR"
end

function COMMAND.exit()
	skynet.exit()
end

skynet.start(function()
	skynet.dispatch("lua", function(_,_, cmd, ...)
		skynet.ret(skynet.pack(COMMAND[cmd](...)))
	end)
end)

else

local function info(fmt, ...)
	skynet.error(string.format(fmt, ...))
end

skynet.start(function()
	local slave = skynet.newservice(SERVICE_NAME, "slave")

	for resp in skynet.request
		{ slave, "lua", "ping", 6, "SLEEP 6" }
		{ slave, "lua", "ping", 5, "SLEEP 5" }
		{ slave, "lua", "ping", 4, "SLEEP 4" }
		{ slave, "lua", "ping", 3, "SLEEP 3" }
		{ slave, "lua", "ping", 2, "SLEEP 2" }
		{ slave, "lua", "ping", 1, "SLEEP 1" }
		:select() do
		info("RESP %s", resp[1])
	end

	-- test timeout
	local req = skynet.request()

	for i = 1, 10 do
		req:add { slave, "lua", "ping", i*10, "SLEEP " .. i, token = i }
	end

	for resp, req in req:select(50) do
		info("RESP %s token<%s>", resp[1], req.token)
	end

	-- test error

	for resp in skynet.request { slave, "lua", "error" } : select() do
		info("Ok : %s", resp.ok)
	end

	-- timeout call

	local req = skynet.request { slave, "lua", "ping", 100 , "PING" }
	for resp in req:select(10) do
		info("%s", resp[1])
	end

	info("Timeout : %s", req.timeout)

	skynet.send(slave, "lua", "exit")
	skynet.exit()
end)

end
