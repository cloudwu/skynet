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

skynet.start(function()
	local slave = skynet.newservice(SERVICE_NAME, "slave")

	for i = 10, 1, -1 do
		local session = skynet.request(slave, "lua", "ping", i, "SLEEP " .. i)
		skynet.error(string.format("Request %d session = %d", i, session))
	end

	for session, ok, resp in skynet.select() do
		skynet.error(string.format("RESP %s session %d", resp, session))
	end

	-- test error and timeout

	for i = 10, 100, 10 do
		local session = skynet.request(slave, "lua", "ping", i, "SLEEP " .. i)
		skynet.error(string.format("Request %d session = %d", i, session))
	end
	local timeout_session = skynet.timer(50)
	local error_session = skynet.request(slave, "lua", "error")
	skynet.error(string.format("Error session = %d", error_session))

	for session, ok, resp in skynet.select() do
		if not ok then
			skynet.error(string.format("Error session = %d , %s", session, resp))
		elseif session == timeout_session then
			skynet.select_discard()
			break
		else
			skynet.error(string.format("RESP %s session %d", resp, session))
		end
	end

	skynet.send(slave, "lua", "exit")
	skynet.exit()
end)

end
