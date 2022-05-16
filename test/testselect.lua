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

	for req, resp in skynet.request
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
	local reqs = skynet.request()

	for i = 1, 10 do
		reqs:add { slave, "lua", "ping", i*10, "SLEEP " .. i, token = i }
	end

	for req, resp in reqs:select(50) do
		info("RESP %s token<%s>", resp[1], req.token)
	end

	-- test error

	for req, resp in skynet.request
		{ slave, "lua", "error" }
		{ slave, "lua", "ping", 0, "PING" }
		: select() do
		if resp then
			info("Ping : %s", resp[1])
		else
			info("Error")
		end
	end

	-- timeout call

	local reqs = skynet.request { slave, "lua", "ping", 100 , "PING" }
	for req, resp in reqs:select(10) do
		info("%s", resp[1])
	end

	info("Timeout : %s", reqs.timeout)

	-- call in select
	for req, resp in skynet.request
		{ slave, "lua", "ping", 20, "CALL 20" }
		{ slave, "lua", "ping", 10, "CALL 10" }
		: select() do
		info("%s", skynet.call( slave, "lua", "ping", 0, "ping in " .. resp[1]) )
		skynet.sleep(50)
	end

	skynet.send(slave, "lua", "exit")
	skynet.exit()
end)

end
